/*
 * Copyright (c) 2006-2015 Luben Tuikov and Douglas Gilbert.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the BSD_LICENSE file.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_cmds_basic.h"
#include "sg_cmds_extra.h"
#include "sg_pt.h"      /* needed for scsi_pt_win32_direct() */
#include "sg_unaligned.h"

/*
 * This utility issues the SCSI READ BUFFER(10 or 16) command to the given
 * device.
 */

static const char * version_str = "1.13 20150105";


#ifndef SG_READ_BUFFER_10_CMD
#define SG_READ_BUFFER_10_CMD 0x3c
#define SG_READ_BUFFER_10_CMDLEN 10
#endif
#ifndef SG_READ_BUFFER_16_CMD
#define SG_READ_BUFFER_16_CMD 0x9b
#define SG_READ_BUFFER_16_CMDLEN 16
#endif

#define SENSE_BUFF_LEN  64       /* Arbitrary, could be larger */
#define DEF_PT_TIMEOUT  60       /* 60 seconds */


static struct option long_options[] = {
        {"16", no_argument, 0, 'L'},
        {"help", no_argument, 0, 'h'},
        {"hex", no_argument, 0, 'H'},
        {"id", required_argument, 0, 'i'},
        {"length", required_argument, 0, 'l'},
        {"long", no_argument, 0, 'L'},
        {"mode", required_argument, 0, 'm'},
        {"offset", required_argument, 0, 'o'},
        {"raw", no_argument, 0, 'r'},
        {"readonly", no_argument, 0, 'R'},
        {"specific", required_argument, 0, 'S'},
        {"verbose", no_argument, 0, 'v'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0},   /* sentinel */
};


static void
usage()
{
    fprintf(stderr, "Usage: "
            "sg_read_buffer [--16] [--help] [--hex] [--id=ID] [--length=LEN]\n"
            "                      [--long] [--mode=MO] [--offset=OFF] "
            "[--raw]\n"
            "                      [--readonly] [--specific=MS] [--verbose] "
            "[--version]\n"
            "                      DEVICE\n"
            "  where:\n"
            "    --16|-L             issue READ BUFFER(16) (def: 10)\n"
            "    --help|-h           print out usage message\n"
            "    --hex|-H            print output in hex\n"
            "    --id=ID|-i ID       buffer identifier (0 (default) to 255)\n"
            "    --length=LEN|-l LEN    length in bytes to read (def: 4)\n"
            "    --long|-L           issue READ BUFFER(16) (def: 10)\n"
            "    --mode=MO|-m MO     read buffer mode, MO is number or "
            "acronym (def: 0)\n"
            "    --offset=OFF|-o OFF    buffer offset (unit: bytes, def: 0)\n"
            "    --raw|-r            output response to stdout\n"
            "    --specific=MS|-S MS    mode specific value; 3 bit field (0 "
            "to 7)\n"
            "    --readonly|-R       open DEVICE read-only (def: read-write)\n"
            "    --verbose|-v        increase verbosity\n"
            "    --version|-V        print version string and exit\n\n"
            "Performs a SCSI READ BUFFER (10 or 16) command. Use '-m xxx' to "
            "list\navailable modes. Numbers given in options are decimal "
            "unless they have\na hex indicator (e.g. a leading '0x').\n"
           );
}


#define MODE_HEADER_DATA        0
#define MODE_VENDOR             1
#define MODE_DATA               2
#define MODE_DESCRIPTOR         3
#define MODE_ECHO_BUFFER        0x0A
#define MODE_ECHO_BDESC         0x0B
#define MODE_EN_EX_ECHO         0x1A
#define MODE_ERR_HISTORY        0x1C

static struct mode_s {
        const char *mode_string;
        int   mode;
        const char *comment;
} modes[] = {
        { "hd",         MODE_HEADER_DATA, "combined header and data"},
        { "vendor",     MODE_VENDOR,    "vendor specific"},
        { "data",       MODE_DATA,      "data"},
        { "desc",       MODE_DESCRIPTOR, "descriptor"},
        { "echo",       MODE_ECHO_BUFFER, "read data from echo buffer "
          "(spc-2)"},
        { "echo_desc",  MODE_ECHO_BDESC, "echo buffer descriptor (spc-2)"},
        { "en_ex",      MODE_EN_EX_ECHO,
          "enable expander communications protocol and echo buffer (spc-3)"},
        { "err_hist",   MODE_ERR_HISTORY, "error history (spc-4)"},
        { NULL,   999, NULL},   /* end sentinel */
};


static void
print_modes(void)
{
    const struct mode_s *mp;

    fprintf(stderr, "The modes parameter argument can be numeric "
                "(hex or decimal)\nor symbolic:\n");
    for (mp = modes; mp->mode_string; ++mp) {
        fprintf(stderr, " %2d (0x%02x)  %-16s%s\n", mp->mode, mp->mode,
                mp->mode_string, mp->comment);
    }
}

/* Invokes a SCSI READ BUFFER(10) command (spc5r02).  Return of 0 -> success,
 * various SG_LIB_CAT_* positive values or -1 -> other errors */
static int
ll_read_buffer_10(int sg_fd, int rb_mode, int rb_mode_sp, int rb_id,
                  uint32_t rb_offset, void * resp, int mx_resp_len,
                  int * residp, int noisy, int verbose)
{
    int k, ret, res, sense_cat;
    uint8_t rb10_cb[SG_READ_BUFFER_10_CMDLEN] =
          {SG_READ_BUFFER_10_CMD, 0, 0, 0,  0, 0, 0, 0, 0, 0};
    uint8_t sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    rb10_cb[1] = (uint8_t)(rb_mode & 0x1f);
    if (rb_mode_sp)
        rb10_cb[1] |= (uint8_t)((rb_mode_sp & 0x7) << 5);
    rb10_cb[2] = (uint8_t)rb_id;
    sg_put_unaligned_be24(rb_offset, rb10_cb + 3);
    sg_put_unaligned_be24(mx_resp_len, rb10_cb + 6);
    if (verbose) {
        fprintf(stderr, "    Read buffer(10) cdb: ");
        for (k = 0; k < SG_READ_BUFFER_10_CMDLEN; ++k)
            fprintf(stderr, "%02x ", rb10_cb[k]);
        fprintf(stderr, "\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        fprintf(stderr, "Read buffer(10): out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, rb10_cb, sizeof(rb10_cb));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)resp, mx_resp_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "Read buffer(10)", res, mx_resp_len,
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
    } else {
        if ((verbose > 2) && (ret > 0)) {
            fprintf(stderr, "    Read buffer(10): response%s\n",
                    (ret > 256 ? ", first 256 bytes" : ""));
            dStrHexErr((const char *)resp, (ret > 256 ? 256 : ret), -1);
        }
        ret = 0;
    }
    if (residp)
        *residp = get_scsi_pt_resid(ptvp);
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/* Invokes a SCSI READ BUFFER(16) command (spc5r02).  Return of 0 -> success,
 * various SG_LIB_CAT_* positive values or -1 -> other errors */
static int
ll_read_buffer_16(int sg_fd, int rb_mode, int rb_mode_sp, int rb_id,
                  uint64_t rb_offset, void * resp, int mx_resp_len,
                  int * residp, int noisy, int verbose)
{
    int k, ret, res, sense_cat;
    uint8_t rb16_cb[SG_READ_BUFFER_16_CMDLEN] =
          {SG_READ_BUFFER_16_CMD, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0};
    uint8_t sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    rb16_cb[1] = (uint8_t)(rb_mode & 0x1f);
    if (rb_mode_sp)
        rb16_cb[1] |= (uint8_t)((rb_mode_sp & 0x7) << 5);
    sg_put_unaligned_be64(rb_offset, rb16_cb + 2);
    sg_put_unaligned_be24(mx_resp_len, rb16_cb + 11);
    rb16_cb[14] = (uint8_t)rb_id;
    if (verbose) {
        fprintf(stderr, "    Read buffer(16) cdb: ");
        for (k = 0; k < SG_READ_BUFFER_16_CMDLEN; ++k)
            fprintf(stderr, "%02x ", rb16_cb[k]);
        fprintf(stderr, "\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        fprintf(stderr, "Read buffer(16): out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, rb16_cb, sizeof(rb16_cb));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)resp, mx_resp_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "Read buffer(16)", res, mx_resp_len,
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
    } else {
        if ((verbose > 2) && (ret > 0)) {
            fprintf(stderr, "    Read buffer(16): response%s\n",
                    (ret > 256 ? ", first 256 bytes" : ""));
            dStrHexErr((const char *)resp, (ret > 256 ? 256 : ret), -1);
        }
        ret = 0;
    }
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

int
main(int argc, char * argv[])
{
    int res, c, len, k;
    int sg_fd = -1;
    int do_help = 0;
    int do_hex = 0;
    int do_long = 0;
    int o_readonly = 0;
    int rb_id = 0;
    int rb_len = 4;
    int rb_mode = 0;
    int rb_mode_sp = 0;
    int64_t ll;
    uint64_t rb_offset = 0;
    int do_raw = 0;
    int resid = 0;
    int verbose = 0;
    int ret = 0;
    const char * device_name = NULL;
    unsigned char * resp;
    const struct mode_s * mp;

    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "hHi:l:Lm:o:rRS:vV", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'h':
        case '?':
            ++do_help;
            break;
        case 'H':
            ++do_hex;
            break;
        case 'i':
            rb_id = sg_get_num(optarg);
            if ((rb_id < 0) || (rb_id > 255)) {
                fprintf(stderr, "argument to '--id' should be in the range "
                        "0 to 255\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            break;
        case 'l':
            rb_len = sg_get_num(optarg);
            if (rb_len < 0) {
                fprintf(stderr, "bad argument to '--length'\n");
                return SG_LIB_SYNTAX_ERROR;
             }
             if (rb_len > 0xffffff) {
                fprintf(stderr, "argument to '--length' must be <= "
                        "0xffffff\n");
                return SG_LIB_SYNTAX_ERROR;
             }
             break;
        case 'L':
            ++do_long;
            break;
        case 'm':
            if (isdigit(*optarg)) {
                rb_mode = sg_get_num(optarg);
                if ((rb_mode < 0) || (rb_mode > 31)) {
                    fprintf(stderr, "argument to '--mode' should be in the "
                            "range 0 to 31\n");
                    return SG_LIB_SYNTAX_ERROR;
                }
            } else {
                len = strlen(optarg);
                for (mp = modes; mp->mode_string; ++mp) {
                    if (0 == strncmp(mp->mode_string, optarg, len)) {
                        rb_mode = mp->mode;
                        break;
                    }
                }
                if (NULL == mp) {
                    print_modes();
                    return SG_LIB_SYNTAX_ERROR;
                }
            }
            break;
        case 'o':
           ll = sg_get_llnum(optarg);
           if (ll < 0) {
                fprintf(stderr, "bad argument to '--offset'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            rb_offset = ll;
            break;
        case 'r':
            ++do_raw;
            break;
        case 'R':
            ++o_readonly;
            break;
        case 'S':
           rb_mode_sp = sg_get_num(optarg);
           if ((rb_mode_sp < 0) || (rb_mode_sp > 7)) {
                fprintf(stderr, "expected argument to '--specific' to be 0 "
                        "to 7\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            fprintf(stderr, "version: %s\n", version_str);
            return 0;
        default:
            fprintf(stderr, "unrecognised option code 0x%x ??\n", c);
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    if (do_help) {
        if (do_help > 1) {
            usage();
            fprintf(stderr, "\n");
            print_modes();
        } else
            usage();
        return 0;
    }
    if (optind < argc) {
        if (NULL == device_name) {
            device_name = argv[optind];
            ++optind;
        }
        if (optind < argc) {
            for (; optind < argc; ++optind)
                fprintf(stderr, "Unexpected extra argument: %s\n",
                        argv[optind]);
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
    }

    if (NULL == device_name) {
        fprintf(stderr, "missing device name!\n");
        usage();
        return SG_LIB_SYNTAX_ERROR;
    }

    if (rb_len > 0) {
        resp = (unsigned char *)malloc(rb_len);
        if (NULL == resp) {
            fprintf(stderr, "unable to allocate %d bytes on the heap\n",
                    rb_len);
            return SG_LIB_CAT_OTHER;
        }
        memset(resp, 0, rb_len);
    } else
        resp = NULL;

    if (do_raw) {
        if (sg_set_binary_mode(STDOUT_FILENO) < 0) {
            perror("sg_set_binary_mode");
            ret = SG_LIB_FILE_ERROR;
            goto fini;
        }
    }

#ifdef SG_LIB_WIN32
#ifdef SG_LIB_WIN32_DIRECT
    if (verbose > 4)
        fprintf(stderr, "Initial win32 SPT interface state: %s\n",
                scsi_pt_win32_spt_state() ? "direct" : "indirect");
    scsi_pt_win32_direct(SG_LIB_WIN32_DIRECT /* SPT pt interface */);
#endif
#endif

    sg_fd = sg_cmds_open_device(device_name, o_readonly, verbose);
    if (sg_fd < 0) {
        fprintf(stderr, "open error: %s: %s\n", device_name,
                safe_strerror(-sg_fd));
        ret = SG_LIB_FILE_ERROR;
        goto fini;
    }

    if (do_long)
        res = ll_read_buffer_16(sg_fd, rb_mode, rb_mode_sp, rb_id, rb_offset,
                                resp, rb_len, &resid, 1, verbose);
    else if (rb_offset > 0xffffff) {
        fprintf(stderr, "--offset value is too large for READ BUFFER(10), "
                "try --16\n");
        ret = SG_LIB_SYNTAX_ERROR;
        goto fini;
    } else
        res = ll_read_buffer_10(sg_fd, rb_mode, rb_mode_sp, rb_id,
                                (uint32_t)rb_offset, resp, rb_len, &resid, 1,
                                verbose);
    if (0 != res) {
        char b[80];

        ret = res;
        if (res > 0) {
            sg_get_category_sense_str(res, sizeof(b), b, verbose);
            fprintf(stderr, "Read buffer(%d) failed: %s\n",
                    (do_long ? 16 : 10), b);
        }
        goto fini;
    }
    if (resid > 0)
        rb_len -= resid;        /* got back less than requested */
    if (rb_len > 0) {
        if (do_raw)
            dStrRaw((const char *)resp, rb_len);
        else if (do_hex || (rb_len < 4))
            dStrHex((const char *)resp, rb_len, ((do_hex > 1) ? 0 : 1));
        else {
            switch (rb_mode) {
            case MODE_DESCRIPTOR:
                k = (resp[1] << 16) | (resp[2] << 8) | resp[3];
                printf("OFFSET BOUNDARY: %d, Buffer offset alignment: "
                       "%d-byte\n", resp[0], (1 << resp[0]));
                printf("BUFFER CAPACITY: %d (0x%x)\n", k, k);
                break;
            case MODE_ECHO_BDESC:
                k = ((resp[2] & 0x1F) << 8) | resp[3];

                printf("EBOS:%d\n", resp[0] & 1 ? 1 : 0);
                printf("Echo buffer capacity: %d (0x%x)\n", k, k);
                break;
            default:
                dStrHex((const char *)resp, rb_len, (verbose > 1 ? 0 : 1));
                break;
            }
        }
    }

fini:
    if (resp)
        free(resp);
    if (sg_fd >= 0) {
        res = sg_cmds_close_device(sg_fd);
        if (res < 0) {
            fprintf(stderr, "close error: %s\n", safe_strerror(-res));
            if (0 == ret)
                return SG_LIB_FILE_ERROR;
        }
    }
    return (ret >= 0) ? ret : SG_LIB_CAT_OTHER;
}
