/*
 *  Copyright (C) 1999-2013 D. Gilbert
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.

    Start/Stop parameter by Kurt Garloff <garloff at suse dot de>, 6/2000
    Sync cache parameter by Kurt Garloff <garloff at suse dot de>, 1/2001
    Guard block device answering sg's ioctls.
                     <dgilbert at interlog dot com> 12/2002
    Convert to SG_IO ioctl so can use sg or block devices in 2.6.* 3/2003

    This utility was written for the Linux 2.4 kernel series. It now
    builds for the Linux 2.6 and 3 kernel series and various other
    Operating Systems.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_cmds_basic.h"


static const char * version_str = "0.59 20130507";  /* sbc3r14; mmc6r01a */

static struct option long_options[] = {
        {"eject", 0, 0, 'e'},
        {"fl", 1, 0, 'f'},
        {"help", 0, 0, 'h'},
        {"immed", 0, 0, 'i'},
        {"load", 0, 0, 'l'},
        {"loej", 0, 0, 'L'},
        {"mod", 1, 0, 'm'},
        {"noflush", 0, 0, 'n'},
        {"new", 0, 0, 'N'},
        {"old", 0, 0, 'O'},
        {"pc", 1, 0, 'p'},
        {"readonly", 0, 0, 'r'},
        {"start", 0, 0, 's'},
        {"stop", 0, 0, 'S'},
        {"verbose", 0, 0, 'v'},
        {"version", 0, 0, 'V'},
        {0, 0, 0, 0},
};

struct opts_t {
    int do_eject;
    int do_fl;
    int do_help;
    int do_immed;
    int do_load;
    int do_loej;
    int do_mod;
    int do_noflush;
    int do_readonly;
    int do_pc;
    int do_start;
    int do_stop;
    int do_verbose;
    int do_version;
    const char * device_name;
    int opt_new;
};

static void
usage()
{
    fprintf(stderr, "Usage: sg_start [--eject] [--fl=FL] [--help] "
            "[--immed] [--load] [--loej]\n"
            "                [--mod=PC_MOD] [--noflush] [--pc=PC] "
            "[--readonly]\n"
            "                [--start] [--stop] [--verbose] "
            "[--version] DEVICE\n"
            "  where:\n"
            "    --eject|-e      stop unit then eject the medium\n"
            "    --fl=FL|-f FL    format layer number (mmc5)\n"
            "    --help|-h       print usage message then exit\n"
            "    --immed|-i      device should return control after "
            "receiving cdb,\n"
            "                    default action is to wait until action "
            "is complete\n"
            "    --load|-l       load medium then start the unit\n"
            "    --loej|-L       load or eject, corresponds to LOEJ bit "
            "in cdb;\n"
            "                    load when START bit also set, else "
            "eject\n"
            "    --mod=PC_MOD|-m PC_MOD    power condition modifier "
            "(def: 0) (sbc)\n"
            "    --noflush|-n    no flush prior to operation that limits "
            "access (sbc)\n"
            "    --pc=PC|-p PC    power condition: 0 (default) -> no "
            "power condition,\n"
            "                    1 -> active, 2 -> idle, 3 -> standby, "
            "5 -> sleep (mmc)\n"
            "    --readonly|-r    open DEVICE read-only (def: read-write)\n"
            "                     recommended if DEVICE is ATA disk\n"
            "    --start|-s      start unit, corresponds to START bit "
            "in cdb,\n"
            "                    default (START=1) if no other options "
            "given\n"
            "    --stop|-S       stop unit (e.g. spin down disk)\n"
            "    --verbose|-v    increase verbosity\n"
            "    --version|-V    print version string then exit\n\n"
            "    Example: 'sg_start --stop /dev/sdb'    stops unit\n"
            "             'sg_start --eject /dev/scd0'  stops unit and "
            "ejects medium\n\n"
            "Performs a SCSI START STOP UNIT command\n"
            );
}

static void
usage_old()
{
    fprintf(stderr, "Usage:  sg_start [0] [1] [--eject] [--fl=FL] "
            "[-i] [--imm=0|1]\n"
            "                 [--load] [--loej] [--mod=PC_MOD] "
            "[--noflush] [--pc=PC]\n"
            "                 [--readonly] [--start] [--stop] [-v] [-V]\n"
            "                 DEVICE\n"
            "  where:\n"
            "    0          stop unit (e.g. spin down a disk or a "
            "cd/dvd)\n"
            "    1          start unit (e.g. spin up a disk or a "
            "cd/dvd)\n"
            "    --eject    stop then eject the medium\n"
            "    --fl=FL    format layer number (mmc5)\n"
            "    -i         return immediately (same as '--imm=1')\n"
            "    --imm=0|1  0->await completion(def), 1->return "
            "immediately\n"
            "    --load     load then start the medium\n"
            "    --loej     load the medium if '-start' option is "
            "also given\n"
            "               or stop unit and eject\n"
            "    --mod=PC_MOD    power condition modifier "
            "(def: 0) (sbc)\n"
            "    --noflush    no flush prior to operation that limits "
            "access (sbc)\n"
            "    --pc=PC    power condition (in hex, default 0 -> no "
            "power condition)\n"
            "               1 -> active, 2 -> idle, 3 -> standby, "
            "5 -> sleep (mmc)\n"
            "    --readonly|-r    open DEVICE read-only (def: read-write)\n"
            "                     recommended if DEVICE is ATA disk\n"
            "    --start    start unit (same as '1'), default "
            "action\n"
            "    --stop     stop unit (same as '0')\n"
            "    -v         verbose (print out SCSI commands)\n"
            "    -V         print version string then exit\n\n"
            "    Example: 'sg_start --stop /dev/sdb'    stops unit\n"
            "             'sg_start --eject /dev/scd0'  stops unit and "
            "ejects medium\n\n"
            "Performs a SCSI START STOP UNIT command\n"
            );
}

static int
process_cl_new(struct opts_t * op, int argc, char * argv[])
{
    int c, n, err;

    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "ef:hilLm:nNOp:rsSvV", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'e':
            ++op->do_eject;
            ++op->do_loej;
            break;
        case 'f':
            n = sg_get_num(optarg);
            if ((n < 0) || (n > 3)) {
                fprintf(stderr, "bad argument to '--fl='\n");
                usage();
                return SG_LIB_SYNTAX_ERROR;
            }
            ++op->do_loej;
            ++op->do_start;
            op->do_fl = n;
            break;
        case 'h':
        case '?':
            ++op->do_help;
            break;
        case 'i':
            ++op->do_immed;
            break;
        case 'l':
            ++op->do_load;
            ++op->do_loej;
            break;
        case 'L':
            ++op->do_loej;
            break;
        case 'm':
            n = sg_get_num(optarg);
            if ((n < 0) || (n > 15)) {
                fprintf(stderr, "bad argument to '--mod='\n");
                usage();
                return SG_LIB_SYNTAX_ERROR;
            }
            op->do_mod = n;
            break;
        case 'n':
            ++op->do_noflush;
            break;
        case 'N':
            break;      /* ignore */
        case 'O':
            op->opt_new = 0;
            return 0;
        case 'p':
            n = sg_get_num(optarg);
            if ((n < 0) || (n > 15)) {
                fprintf(stderr, "bad argument to '--pc='\n");
                usage();
                return SG_LIB_SYNTAX_ERROR;
            }
            op->do_pc = n;
            break;
        case 'r':
            ++op->do_readonly;
            break;
        case 's':
            ++op->do_start;
            break;
        case 'S':
            ++op->do_stop;
            break;
        case 'v':
            ++op->do_verbose;
            break;
        case 'V':
            ++op->do_version;
            break;
        default:
            fprintf(stderr, "unrecognised option code %c [0x%x]\n", c, c);
            if (op->do_help)
                break;
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    err = 0;
    for (; optind < argc; ++optind) {
        if (1 == strlen(argv[optind])) {
            if (0 == strcmp("0", argv[optind])) {
                ++op->do_stop;
                continue;
            } else if (0 == strcmp("1", argv[optind])) {
                ++op->do_start;
                continue;
            }
        }
        if (NULL == op->device_name)
            op->device_name = argv[optind];
        else {
            fprintf(stderr, "Unexpected extra argument: %s\n", argv[optind]);
            ++err;
        }
    }
    if (err) {
        usage();
        return SG_LIB_SYNTAX_ERROR;
    } else
        return 0;
}

static int
process_cl_old(struct opts_t * op, int argc, char * argv[])
{
    int k, jmp_out, plen, num;
    int ambigu = 0;
    int startstop = -1;
    unsigned int u;
    const char * cp;

    for (k = 1; k < argc; ++k) {
        cp = argv[k];
        plen = strlen(cp);
        if (plen <= 0)
            continue;
        if ('-' == *cp) {
            for (--plen, ++cp, jmp_out = 0; plen > 0;
                 --plen, ++cp) {
                switch (*cp) {
                case 'i':
                    if ('\0' == *(cp + 1))
                        op->do_immed = 1;
                    else
                        jmp_out = 1;
                    break;
                case 'r':
                    ++op->do_readonly;
                    break;
                case 'v':
                    ++op->do_verbose;
                    break;
                case 'V':
                    ++op->do_version;
                    break;
                case 'h':
                case '?':
                    ++op->do_help;
                    break;
                case 'N':
                    op->opt_new = 1;
                    return 0;
                case 'O':
                    break;
                case '-':
                    ++cp;
                    --plen;
                    jmp_out = 1;
                    break;
                default:
                    jmp_out = 1;
                    break;
                }
                if (jmp_out)
                    break;
            }
            if (plen <= 0)
                continue;

            if (0 == strncmp(cp, "eject", 5)) {
                op->do_loej = 1;
                if (startstop == 1)
                    ambigu = 1;
                else
                    startstop = 0;
            } else if (0 == strncmp("fl=", cp, 3)) {
                num = sscanf(cp + 3, "%x", &u);
                if (1 != num) {
                    fprintf(stderr, "Bad value after 'fl=' option\n");
                    usage_old();
                    return SG_LIB_SYNTAX_ERROR;
                }
                startstop = 1;
                op->do_loej = 1;
                op->do_fl = u;
            } else if (0 == strncmp("imm=", cp, 4)) {
                num = sscanf(cp + 4, "%x", &u);
                if ((1 != num) || (u > 1)) {
                    fprintf(stderr, "Bad value after 'imm=' option\n");
                    usage_old();
                    return SG_LIB_SYNTAX_ERROR;
                }
                op->do_immed = u;
            } else if (0 == strncmp(cp, "load", 4)) {
                op->do_loej = 1;
                if (startstop == 0)
                    ambigu = 1;
                else
                    startstop = 1;
            } else if (0 == strncmp(cp, "loej", 4))
                op->do_loej = 1;
            else if (0 == strncmp("pc=", cp, 3)) {
                num = sscanf(cp + 3, "%x", &u);
                if ((1 != num) || (u > 15)) {
                    fprintf(stderr, "Bad value after after 'pc=' option\n");
                    usage_old();
                    return SG_LIB_SYNTAX_ERROR;
                }
                op->do_pc = u;
            } else if (0 == strncmp("mod=", cp, 4)) {
                num = sscanf(cp + 3, "%x", &u);
                if (1 != num) {
                    fprintf(stderr, "Bad value after 'mod=' option\n");
                    usage_old();
                    return SG_LIB_SYNTAX_ERROR;
                }
                op->do_mod = u;
            } else if (0 == strncmp(cp, "noflush", 7)) {
                op->do_noflush = 1;
            } else if (0 == strncmp(cp, "start", 5)) {
                if (startstop == 0)
                    ambigu = 1;
                else
                    startstop = 1;
            } else if (0 == strncmp(cp, "stop", 4)) {
                if (startstop == 1)
                    ambigu = 1;
                else
                    startstop = 0;
            } else if (0 == strncmp(cp, "old", 3))
                ;
            else if (jmp_out) {
                fprintf(stderr, "Unrecognized option: %s\n", cp);
                usage_old();
                return SG_LIB_SYNTAX_ERROR;
            }
        } else if (0 == strcmp("0", cp)) {
            if (1 == startstop)
                ambigu = 1;
            else
                startstop = 0;
        } else if (0 == strcmp("1", cp)) {
            if (0 == startstop)
                ambigu = 1;
            else
                startstop = 1;
        } else if (0 == op->device_name)
                op->device_name = cp;
        else {
            fprintf(stderr, "too many arguments, got: %s, not "
                    "expecting: %s\n", op->device_name, cp);
            usage_old();
            return SG_LIB_SYNTAX_ERROR;
        }
        if (ambigu) {
            fprintf(stderr, "please, only one of 0, 1, --eject, "
                    "--load, --start or --stop\n");
            usage_old();
            return SG_LIB_SYNTAX_ERROR;
        } else if (0 == startstop)
            ++op->do_stop;
        else if (1 == startstop)
            ++op->do_start;
    }
    return 0;
}

static int
process_cl(struct opts_t * op, int argc, char * argv[])
{
    int res;
    char * cp;

    cp = getenv("SG3_UTILS_OLD_OPTS");
    if (cp) {
        op->opt_new = 0;
        res = process_cl_old(op, argc, argv);
        if ((0 == res) && op->opt_new)
            res = process_cl_new(op, argc, argv);
    } else {
        op->opt_new = 1;
        res = process_cl_new(op, argc, argv);
        if ((0 == res) && (0 == op->opt_new))
            res = process_cl_old(op, argc, argv);
    }
    return res;
}


int
main(int argc, char * argv[])
{
    int fd, res;
    int ret = 0;
    struct opts_t opts;
    struct opts_t * op;

    op = &opts;
    memset(op, 0, sizeof(opts));
    op->do_fl = -1;    /* only when >= 0 set FL bit */
    res = process_cl(op, argc, argv);
    if (res)
        return SG_LIB_SYNTAX_ERROR;
    if (op->do_help) {
        if (op->opt_new)
            usage();
        else
            usage_old();
        return 0;
    }
    if (op->do_version) {
        fprintf(stderr, "Version string: %s\n", version_str);
        return 0;
    }

    if (op->do_start && op->do_stop) {
        fprintf(stderr, "Ambiguous to give both '--start' and '--stop'\n");
        return SG_LIB_SYNTAX_ERROR;
    }
    if (op->do_load && op->do_eject) {
        fprintf(stderr, "Ambiguous to give both '--load' and '--eject'\n");
        return SG_LIB_SYNTAX_ERROR;
    }
    if (op->do_load)
       op->do_start = 1;
    else if ((op->do_eject) || (op->do_stop))
       op->do_start = 0;
    else if (op->opt_new && op->do_loej && (0 == op->do_start))
        op->do_start = 1;      /* --loej alone in new interface is load */
    else if ((0 == op->do_loej) && (-1 == op->do_fl) && (0 == op->do_pc))
       op->do_start = 1;
    /* default action is to start when no other active options */

    if (0 == op->device_name) {
        fprintf(stderr, "No DEVICE argument given\n");
        if (op->opt_new)
            usage();
        else
            usage_old();
        return SG_LIB_SYNTAX_ERROR;
    }

    if (op->do_fl >= 0) {
        if (op->do_start == 0) {
            fprintf(stderr, "Giving '--fl=FL' with '--stop' (or "
                    "'--eject') is invalid\n");
            return SG_LIB_SYNTAX_ERROR;
        }
        if (op->do_pc > 0) {
            fprintf(stderr, "Giving '--fl=FL' with '--pc=PC' "
                    "when PC is non-zero is invalid\n");
            return SG_LIB_SYNTAX_ERROR;
        }
    }

    fd = sg_cmds_open_device(op->device_name, op->do_readonly,
                             op->do_verbose);
    if (fd < 0) {
        fprintf(stderr, "Error trying to open %s: %s\n",
                op->device_name, safe_strerror(-fd));
        return SG_LIB_FILE_ERROR;
    }

    res = 0;
    if (op->do_fl >= 0)
        res = sg_ll_start_stop_unit(fd, op->do_immed, op->do_fl, 0 /* pc */,
                                    1 /* fl */, 1 /* loej */,
                                    1 /*start */, 1 /* noisy */,
                                    op->do_verbose);
    else if (op->do_pc > 0)
        res = sg_ll_start_stop_unit(fd, op->do_immed, op->do_mod,
                                    op->do_pc, op->do_noflush, 0, 0, 1,
                                    op->do_verbose);
    else
        res = sg_ll_start_stop_unit(fd, op->do_immed, 0, 0, op->do_noflush,
                                    op->do_loej, op->do_start, 1,
                                    op->do_verbose);
    ret = res;
    if (res) {
        if (op->do_verbose < 2) {
            char b[80];

            sg_get_category_sense_str(res, sizeof(b), b, op->do_verbose);
            fprintf(stderr, "%s\n", b);
        }
        fprintf(stderr, "START STOP UNIT command failed\n");
    }
    res = sg_cmds_close_device(fd);
    if ((res < 0) && (0 == ret))
        return SG_LIB_FILE_ERROR;
    return (ret >= 0) ? ret : SG_LIB_CAT_OTHER;
}
