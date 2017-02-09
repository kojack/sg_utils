/*
 * Copyright (c) 2014 Douglas Gilbert.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the BSD_LICENSE file.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_lib_data.h"
#include "sg_pt.h"
#include "sg_cmds_basic.h"
#include "sg_unaligned.h"

/* A utility program originally written for the Linux OS SCSI subsystem.
 *
 *
 * This program issues the SCSI REPORT ZONES command to the given SCSI device
 * and decodes the response. Based on zbc-r02.pdf
 */

static const char * version_str = "1.04 20141215";

#define MAX_RZONES_BUFF_LEN (1024 * 1024)
#define DEF_RZONES_BUFF_LEN (1024 * 8)

#define SG_ZONING_IN_CMDLEN 16

#define REPORT_ZONES_SA 0x0

#define SENSE_BUFF_LEN 64       /* Arbitrary, could be larger */
#define DEF_PT_TIMEOUT  60      /* 60 seconds */


static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"hex", no_argument, 0, 'H'},
        {"maxlen", required_argument, 0, 'm'},
        {"raw", no_argument, 0, 'r'},
        {"readonly", no_argument, 0, 'R'},
        {"report", required_argument, 0, 'o'},
        {"start", required_argument, 0, 's'},
        {"verbose", no_argument, 0, 'v'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0},
};


#ifdef __GNUC__
static int pr2serr(const char * fmt, ...)
        __attribute__ ((format (printf, 1, 2)));
#else
static int pr2serr(const char * fmt, ...);
#endif


static int
pr2serr(const char * fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = vfprintf(stderr, fmt, args);
    va_end(args);
    return n;
}

static void
usage()
{
    pr2serr("Usage: "
            "sg_rep_zones  [--help] [--hex] [--maxlen=LEN] [--raw]\n"
            "                     [--readonly] [--report=OPT] "
            "[--start=LBA]\n"
            "                     [--verbose] [--version] DEVICE\n");
    pr2serr("  where:\n"
            "    --help|-h          print out usage message\n"
            "    --hex|-H           output response in hexadecimal; used "
            "twice\n"
            "                       shows decoded values in hex\n"
            "    --maxlen=LEN|-m LEN    max response length (allocation "
            "length in cdb)\n"
            "                           (def: 0 -> 8192 bytes)\n"
            "    --raw|-r           output response in binary\n"
            "    --readonly|-R      open DEVICE read-only (def: read-write)\n"
            "    --report=OPT|-o OP    reporting option (def: 0)\n"
            "    --start=LBA|-s LBA    report zones from the LBA (def: 0)\n"
            "                          need not be a zone starting LBA\n"
            "    --verbose|-v       increase verbosity\n"
            "    --version|-V       print version string and exit\n\n"
            "Performs a SCSI REPORT ZONES command.\n");
}

/* Invokes a SCSI REPORT ZONES command (ZBC).  Return of 0 -> success,
 * various SG_LIB_CAT_* positive values or -1 -> other errors */
static int
sg_ll_report_zones(int sg_fd, uint64_t zs_lba, int report_opts, void * resp,
                   int mx_resp_len, int * residp, int noisy, int verbose)
{
    int k, ret, res, sense_cat;
    unsigned char rzCmdBlk[SG_ZONING_IN_CMDLEN] =
          {SG_ZONING_IN, REPORT_ZONES_SA, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    sg_put_unaligned_be64(zs_lba, rzCmdBlk + 2);
    sg_put_unaligned_be32((uint32_t)mx_resp_len, rzCmdBlk + 10);
    rzCmdBlk[14] = report_opts & 0xf;
    if (verbose) {
        pr2serr("    Report zones cdb: ");
        for (k = 0; k < SG_ZONING_IN_CMDLEN; ++k)
            pr2serr("%02x ", rzCmdBlk[k]);
        pr2serr("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2serr("Report zones: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, rzCmdBlk, sizeof(rzCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)resp, mx_resp_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "report zones", res, mx_resp_len,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret)
        ;
    else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else
        ret = 0;
    if (residp)
        *residp = get_scsi_pt_resid(ptvp);
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

static void
dStrRaw(const char* str, int len)
{
    int k;

    for (k = 0 ; k < len; ++k)
        printf("%c", str[k]);
}

static const char *
zone_type_str(int zt, char * b, int blen, int vb)
{
    const char * cp;

    if (NULL == b)
        return "zone_type_str: NULL ptr)";
    switch (zt) {
    case 1:
        cp = "Conventional";
        break;
    case 2:
        cp = "Sequential write required";
        break;
    case 3:
        cp = "Sequential write preferred";
        break;
    default:
        cp = NULL;
        break;
    }
    if (cp) {
        if (vb)
            snprintf(b, blen, "%s [0x%x]", cp, zt);
        else
            snprintf(b, blen, "%s", cp);
    } else
        snprintf(b, blen, "Reserved [0x%x]", zt);
    return b;
}

static const char *
zone_condition_str(int zc, char * b, int blen, int vb)
{
    const char * cp;

    if (NULL == b)
        return "zone_condition_str: NULL ptr)";
    switch (zc) {
    case 0:
        cp = "No write pointer";
        break;
    case 1:
        cp = "Empty";
        break;
    case 2:
        cp = "Open";
        break;
    case 0xd:
        cp = "Read only";
        break;
    case 0xe:
        cp = "Full";
        break;
    case 0xf:
        cp = "Offline";
        break;
    default:
        cp = NULL;
        break;
    }
    if (cp) {
        if (vb)
            snprintf(b, blen, "%s [0x%x]", cp, zc);
        else
            snprintf(b, blen, "%s", cp);
    } else
        snprintf(b, blen, "Reserved [0x%x]", zc);
    return b;
}

static const char * same_desc_arr[4] = {
    "zone type and length may differ in each descriptor",
    "zone type and length same in each descriptor",
    "zone type and length same apart from length in last descriptor",
    "Reserved",
};


int
main(int argc, char * argv[])
{
    int sg_fd, k, res, c, zl_len, len, zones, resid, rlen, zt, zc, same;
    int do_hex = 0;
    int maxlen = 0;
    int do_raw = 0;
    int o_readonly = 0;
    int reporting_opt = 0;
    int verbose = 0;
    uint64_t st_lba = 0;
    int64_t ll;
    const char * device_name = NULL;
    unsigned char * reportZonesBuff = NULL;
    unsigned char * ucp;
    int ret = 0;
    char b[80];

    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "hHm:o:rRs:vV", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'h':
        case '?':
            usage();
            return 0;
        case 'H':
            ++do_hex;
            break;
        case 'm':
            maxlen = sg_get_num(optarg);
            if ((maxlen < 0) || (maxlen > MAX_RZONES_BUFF_LEN)) {
                pr2serr("argument to '--maxlen' should be %d or "
                        "less\n", MAX_RZONES_BUFF_LEN);
                return SG_LIB_SYNTAX_ERROR;
            }
            break;
        case 'o':
           reporting_opt = sg_get_num(optarg);
           if ((reporting_opt < 0) || (reporting_opt > 15)) {
                pr2serr("bad argument to '--report=OPT', expect 0 to "
                        "15\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            break;
        case 'r':
            ++do_raw;
            break;
        case 'R':
            ++o_readonly;
            break;
        case 's':
            ll = sg_get_llnum(optarg);
            if (-1 == ll) {
                fprintf(stderr, "bad argument to '--start=LBA'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            st_lba = (uint64_t)ll;
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            pr2serr("version: %s\n", version_str);
            return 0;
        default:
            pr2serr("unrecognised option code 0x%x ??\n", c);
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    if (optind < argc) {
        if (NULL == device_name) {
            device_name = argv[optind];
            ++optind;
        }
        if (optind < argc) {
            for (; optind < argc; ++optind)
                pr2serr("Unexpected extra argument: %s\n",
                        argv[optind]);
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
    }

    if (NULL == device_name) {
        pr2serr("missing device name!\n");
        usage();
        return SG_LIB_SYNTAX_ERROR;
    }

    if (do_raw) {
        if (sg_set_binary_mode(STDOUT_FILENO) < 0) {
            perror("sg_set_binary_mode");
            return SG_LIB_FILE_ERROR;
        }
    }

    sg_fd = sg_cmds_open_device(device_name, o_readonly, verbose);
    if (sg_fd < 0) {
        pr2serr("open error: %s: %s\n", device_name,
                safe_strerror(-sg_fd));
        return SG_LIB_FILE_ERROR;
    }

    if (0 == maxlen)
        maxlen = DEF_RZONES_BUFF_LEN;
    reportZonesBuff = (unsigned char *)calloc(1, maxlen);
    if (NULL == reportZonesBuff) {
        pr2serr("unable to malloc %d bytes\n", maxlen);
        return SG_LIB_CAT_OTHER;
    }

    res = sg_ll_report_zones(sg_fd, st_lba, reporting_opt, reportZonesBuff,
                             maxlen, &resid, 1, verbose);
    ret = res;
    if (0 == res) {
        rlen = maxlen - resid;
        if (rlen < 4) {
            pr2serr("Response length (%d) too short\n", rlen);
            ret = SG_LIB_CAT_MALFORMED;
            goto the_end;
        }
        zl_len = sg_get_unaligned_be32(reportZonesBuff + 0) + 64;
        if (zl_len > rlen) {
            if (verbose)
                pr2serr("zl_len available is %d, response length is %d\n",
                        zl_len, rlen);
            len = rlen;
        } else
            len = zl_len;
        if (do_raw) {
            dStrRaw((const char *)reportZonesBuff, len);
            goto the_end;
        }
        if (do_hex && (2 != do_hex)) {
            dStrHex((const char *)reportZonesBuff, len,
                    ((1 == do_hex) ? 1 : -1));
            goto the_end;
        }
        printf("Report zones response:\n");
        if (len < 64) {
            pr2serr("Zone length [%d] too short (perhaps after truncation\n)",
                    len);
            ret = SG_LIB_CAT_MALFORMED;
            goto the_end;
        }
        same = reportZonesBuff[4] & 3;
        printf("  Same=%d: %s\n\n", same, same_desc_arr[same]);
        zones = (len - 64) / 64;
        for (k = 0, ucp = reportZonesBuff + 64; k < zones; ++k, ucp += 64) {
            printf(" Zone descriptor: %d\n", k);
            if (do_hex) {
                dStrHex((const char *)ucp, len, -1);
                continue;
            }
            zt = ucp[0] & 0xf;
            zc = (ucp[1] >> 4) & 0xf;
            printf("   Zone type: %s\n", zone_type_str(zt, b, sizeof(b),
                   verbose));
            printf("   Zone condition: %s\n", zone_condition_str(zc, b,
                   sizeof(b), verbose));
            printf("   Non_seq: %d\n", !!(ucp[1] & 0x2));
            printf("   Reset: %d\n", ucp[1] & 0x1);
            printf("   Zone Length: 0x%" PRIx64 "\n",
                   sg_get_unaligned_be64(ucp + 8));
            printf("   Zone start LBA: 0x%" PRIx64 "\n",
                   sg_get_unaligned_be64(ucp + 16));
            printf("   Write pointer LBA: 0x%" PRIx64 "\n",
                   sg_get_unaligned_be64(ucp + 24));
        }
        if ((64 + (64 * zones)) < zl_len)
            printf("\n>>> Beware: Zone list truncated, may need another "
                   "call\n");
    } else if (SG_LIB_CAT_INVALID_OP == res)
        pr2serr("Report zones command not supported\n");
    else {
        sg_get_category_sense_str(res, sizeof(b), b, verbose);
        pr2serr("Report zones command: %s\n", b);
    }

the_end:
    if (reportZonesBuff)
        free(reportZonesBuff);
    res = sg_cmds_close_device(sg_fd);
    if (res < 0) {
        pr2serr("close error: %s\n", safe_strerror(-res));
        if (0 == ret)
            return SG_LIB_FILE_ERROR;
    }
    return (ret >= 0) ? ret : SG_LIB_CAT_OTHER;
}
