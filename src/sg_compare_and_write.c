/*
*  Copyright (c) 2012-2015, Kaminario Technologies LTD
*  All rights reserved.
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright
*        notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright
*        notice, this list of conditions and the following disclaimer in the
*        documentation and/or other materials provided with the distribution.
*    * Neither the name of the <organization> nor the
*        names of its contributors may be used to endorse or promote products
*        derived from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
*  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
*  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
*  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
*  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
*  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
*  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
*  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This command performs a SCSI COMPARE AND WRITE. See SBC-3 at
 * http://www.t10.org
 *
 */

#ifndef __sun
#define _XOPEN_SOURCE 500
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <getopt.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_cmds_basic.h"
#include "sg_pt.h"

static const char * version_str = "1.10 20150511";

#define DEF_BLOCK_SIZE 512
#define DEF_NUM_BLOCKS (1)
#define DEF_BLOCKS_PER_TRANSFER 8
#define DEF_TIMEOUT_SECS 60

#define COMPARE_AND_WRITE_OPCODE (0x89)
#define COMPARE_AND_WRITE_CDB_SIZE (16)

#define SENSE_BUFF_LEN 64       /* Arbitrary, could be larger */

#define ME "sg_compare_and_write: "

static struct option long_options[] = {
        {"dpo", no_argument, 0, 'd'},
        {"fua", no_argument, 0, 'f'},
        {"fua_nv", no_argument, 0, 'F'},
        {"group", required_argument, 0, 'g'},
        {"help", no_argument, 0, 'h'},
        {"in", required_argument, 0, 'i'},
        {"inc", required_argument, 0, 'C'},
        {"inw", required_argument, 0, 'D'},
        {"lba", required_argument, 0, 'l'},
        {"num", required_argument, 0, 'n'},
        {"quiet", no_argument, 0, 'q'},
        {"timeout", required_argument, 0, 't'},
        {"verbose", no_argument, 0, 'v'},
        {"version", no_argument, 0, 'V'},
        {"wrprotect", required_argument, 0, 'w'},
        {"xferlen", required_argument, 0, 'x'},
        {0, 0, 0, 0},
};

struct caw_flags {
        int dpo;
        int fua;
        int fua_nv;
        int group;
        int wrprotect;
};

struct opts_t {
        const char * ifn;
        const char * wfn;
        int wfn_given;
        uint64_t lba;
        int numblocks;
        int quiet;
        int verbose;
        int timeout;
        int xfer_len;
        const char * device_name;
        struct caw_flags flags;
};


static void
usage()
{
        fprintf(stderr, "Usage: "
                "sg_compare_and_write [--dpo] [--fua] [--fua_nv] "
                "[--group=GN] [--help]\n"
                "                            --in=IF [--inw=WF] --lba=LBA "
                "[--num=NUM]\n"
                "                            [--quiet] [--timeout=TO] "
                "[--verbose] [--version]\n"
                "                            [--wrpotect=WP] [--xferlen=LEN] "
                "DEVICE\n"
                "  where:\n"
                "    --dpo|-d            set the dpo bit in cdb (def: "
                "clear)\n"
                "    --fua|-f            set the fua bit in cdb (def: "
                "clear)\n"
                "    --fua_nv|-F         set the fua_nv bit in cdb (def: "
                "clear)\n"
                "    --group=GN|-g GN    GN is GROUP NUMBER to set in "
                "cdb (def: 0)\n"
                "    --help|-h           print out usage message\n"
                "    --in=IF|-i IF       IF is a file containing a compare "
                "buffer and\n"
                "                        optionally a write buffer (when "
                "--inw=WF is\n"
                "                        not given)\n"
                "    --inw=WF|-D WF      WF is a file containing a write "
                "buffer\n"
                "    --lba=LBA|-l LBA    LBA of the first block to compare "
                "and write\n"
                "    --num=NUM|-n NUM    number of blocks to "
                "compare/write (def: 1)\n"
                "    --quiet|-q          suppress MISCOMPARE report to "
                "stderr,\n"
                "                        still sets exit status of 14\n"
                "    --timeout=TO|-t TO    timeout for the command "
                "(def: 60 secs)\n"
                "    --verbose|-v        increase verbosity (use '-vv' for "
                "more)\n"
                "    --version|-V        print version string then exit\n"
                "    --wrprotect=WP|-w WP    write protect information "
                "(def: 0)\n"
                "    --xferlen=LEN|-x LEN    number of bytes to transfer. "
                "Default is\n"
                "                            (2 * NUM * 512) or 1024 when "
                "NUM is 1\n"
                "\n"
                "Performs a SCSI COMPARE AND WRITE operation.\n");
}

static int
parse_args(int argc, char* argv[], struct opts_t * op)
{
        int c;
        int lba_given = 0;
        int if_given = 0;
        int64_t ll;

        op->numblocks = DEF_NUM_BLOCKS;
        /* COMPARE AND WRITE defines 2*buffers compare + write */
        op->xfer_len = 0;
        op->timeout = DEF_TIMEOUT_SECS;
        op->device_name = NULL;
        while (1) {
                int option_index = 0;

                c = getopt_long(argc, argv, "C:dD:fFg:hi:l:n:qt:vVw:x:",
                                long_options, &option_index);
                if (c == -1)
                        break;

                switch (c) {
                case 'C':
                case 'i':
                        op->ifn = optarg;
                        if_given = 1;
                        break;
                case 'd':
                        op->flags.dpo = 1;
                        break;
                case 'D':
                        op->wfn = optarg;
                        op->wfn_given = 1;
                        break;
                case 'F':
                        op->flags.fua_nv = 1;
                        break;
                case 'f':
                        op->flags.fua = 1;
                        break;
                case 'g':
                        op->flags.group = sg_get_num(optarg);
                        if ((op->flags.group < 0) ||
                            (op->flags.group > 31))  {
                                fprintf(stderr, "argument to '--group' "
                                        "expected to be 0 to 31\n");
                                goto out_err_no_usage;
                        }
                        break;
                case 'h':
                case '?':
                        usage();
                        exit(0);
                case 'l':
                        ll = sg_get_llnum(optarg);
                        if (-1 == ll) {
                                fprintf(stderr, "bad argument to '--lba'\n");
                                goto out_err_no_usage;
                        }
                        op->lba = (uint64_t)ll;
                        lba_given = 1;
                        break;
                case 'n':
                        op->numblocks = sg_get_num(optarg);
                        if ((op->numblocks < 0) || (op->numblocks > 255))  {
                                fprintf(stderr, "bad argument to '--num', "
                                        "expect 0 to 255\n");
                                goto out_err_no_usage;
                        }
                        break;
                case 'q':
                        ++op->quiet;
                        break;
                case 't':
                        op->timeout = sg_get_num(optarg);
                        if (op->timeout < 0)  {
                                fprintf(stderr, "bad argument to "
                                        "'--timeout'\n");
                                goto out_err_no_usage;
                        }
                        break;
                case 'v':
                        ++op->verbose;
                        break;
                case 'V':
                        fprintf(stderr, ME "version: %s\n", version_str);
                        exit(0);
                case 'w':
                        op->flags.wrprotect = sg_get_num(optarg);
                        if (op->flags.wrprotect >> 3) {
                                fprintf(stderr, "bad argument to "
                                        "'--wrprotect' not in range 0-7\n");
                                goto out_err_no_usage;
                        }
                        break;
                case 'x':
                        op->xfer_len = sg_get_num(optarg);
                        if (op->xfer_len < 0) {
                                fprintf(stderr, "bad argument to "
                                        "'--xferlen'\n");
                                goto out_err_no_usage;
                        }
                        break;
                default:
                        fprintf(stderr, "unrecognised option code 0x%x ??\n",
                                c);
                        goto out_err;
                }
        }
        if (optind < argc) {
                if (NULL == op->device_name) {
                        op->device_name = argv[optind];
                        ++optind;
                }
                if (optind < argc) {
                        for (; optind < argc; ++optind)
                                fprintf(stderr, "Unexpected extra argument: "
                                        "%s\n", argv[optind]);
                        goto out_err;
                }
        }
        if (NULL == op->device_name) {
                fprintf(stderr, "missing device name!\n");
                goto out_err;
        }
        if (!if_given) {
                fprintf(stderr, "missing input file\n");
                goto out_err;
        }
        if (!lba_given) {
                fprintf(stderr, "missing lba\n");
                goto out_err;
        }
        if (0 == op->xfer_len)
            op->xfer_len = 2 * op->numblocks * DEF_BLOCK_SIZE;
        return 0;

out_err:
        usage();

out_err_no_usage:
        exit(1);
}

#define FLAG_FUA        (0x8)
#define FLAG_FUA_NV     (0x2)
#define FLAG_DPO        (0x10)
#define WRPROTECT_MASK  (0x7)
#define WRPROTECT_SHIFT (5)

static int
sg_build_scsi_cdb(unsigned char * cdbp, unsigned int blocks,
                  int64_t start_block, struct caw_flags flags)
{
        memset(cdbp, 0, COMPARE_AND_WRITE_CDB_SIZE);
        cdbp[0] = COMPARE_AND_WRITE_OPCODE;
        cdbp[1] = (flags.wrprotect & WRPROTECT_MASK) << WRPROTECT_SHIFT;
        if (flags.dpo)
                cdbp[1] |= FLAG_DPO;
        if (flags.fua)
                cdbp[1] |= FLAG_FUA;
        if (flags.fua_nv)
                cdbp[1] |= FLAG_FUA_NV;
        cdbp[2] = (unsigned char)((start_block >> 56) & 0xff);
        cdbp[3] = (unsigned char)((start_block >> 48) & 0xff);
        cdbp[4] = (unsigned char)((start_block >> 40) & 0xff);
        cdbp[5] = (unsigned char)((start_block >> 32) & 0xff);
        cdbp[6] = (unsigned char)((start_block >> 24) & 0xff);
        cdbp[7] = (unsigned char)((start_block >> 16) & 0xff);
        cdbp[8] = (unsigned char)((start_block >> 8) & 0xff);
        cdbp[9] = (unsigned char)(start_block & 0xff);
        /* cdbp[10-12] are reserved */
        cdbp[13] = (unsigned char)(blocks & 0xff);
        cdbp[14] = (unsigned char)(flags.group & 0x1f);
        return 0;
}

/* Returns 0 for success, SG_LIB_CAT_MISCOMPARE if compare fails,
 * various other SG_LIB_CAT_*, otherwise -1 . */
static int
sg_compare_and_write(int sg_fd, unsigned char * buff, int blocks,
                     int64_t lba, int xfer_len, struct caw_flags flags,
                     int noisy, int verbose)
{
        int k, sense_cat, valid, slen, res, ret;
        unsigned char cawCmd[COMPARE_AND_WRITE_CDB_SIZE];
        unsigned char sense_b[SENSE_BUFF_LEN];
        struct sg_pt_base * ptvp;
        uint64_t ull = 0;

        if (sg_build_scsi_cdb(cawCmd, blocks, lba, flags)) {
                fprintf(stderr, ME "bad cdb build, lba=0x%" PRIx64 ", "
                        "blocks=%d\n", lba, blocks);
                return -1;
        }
        ptvp = construct_scsi_pt_obj();
        if (NULL == ptvp) {
                fprintf(stderr, "Could not construct scsit_pt_obj, out of "
                        "memory\n");
                return -1;
        }

        set_scsi_pt_cdb(ptvp, cawCmd, COMPARE_AND_WRITE_CDB_SIZE);
        set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
        set_scsi_pt_data_out(ptvp, buff, xfer_len);
        if (verbose > 1) {
                fprintf(stderr, "    Compare and write cdb: ");
                for (k = 0; k < COMPARE_AND_WRITE_CDB_SIZE; ++k)
                        fprintf(stderr, "%02x ", cawCmd[k]);
                fprintf(stderr, "\n");
        }
        if ((verbose > 2) && (xfer_len > 0)) {
                fprintf(stderr, "    Data-out buffer contents:\n");
                dStrHexErr((const char *)buff, xfer_len, 1);
        }
        res = do_scsi_pt(ptvp, sg_fd, DEF_TIMEOUT_SECS, verbose);
        ret = sg_cmds_process_resp(ptvp, "COMPARE AND WRITE", res, 0,
                                   sense_b, noisy, verbose,
                                   &sense_cat);
        if (-1 == ret)
                ;
        else if (-2 == ret) {
                switch (sense_cat) {
                case SG_LIB_CAT_RECOVERED:
                case SG_LIB_CAT_NO_SENSE:
                        ret = 0;
                        break;
                case SG_LIB_CAT_MEDIUM_HARD:
                        slen = get_scsi_pt_sense_len(ptvp);
                        valid = sg_get_sense_info_fld(sense_b, slen,
                                                      &ull);
                        if (valid)
                                fprintf(stderr, "Medium or hardware "
                                        "error starting at lba=%"
                                        PRIu64 " [0x%" PRIx64 "]\n",
                                        ull, ull);
                        else
                                fprintf(stderr, "Medium or hardware "
                                        "error\n");
                        ret = sense_cat;
                        break;
                case SG_LIB_CAT_MISCOMPARE:
                        ret = sense_cat;
                        if (! (noisy || verbose))
                                break;
                        slen = get_scsi_pt_sense_len(ptvp);
                        valid = sg_get_sense_info_fld(sense_b, slen, &ull);
                        if (valid)
                                fprintf(stderr, "Miscompare at byte offset: %"
                                        PRIu64 " [0x%" PRIx64 "]\n", ull,
                                        ull);
                        else
                                fprintf(stderr, "Miscompare reported\n");
                        break;
                default:
                        ret = sense_cat;
                        break;
                }
        } else
                ret = 0;

        destruct_scsi_pt_obj(ptvp);
        return ret;
}

static int
open_if(const char * fn, int got_stdin)
{
        int fd;

        if (got_stdin)
                fd = STDIN_FILENO;
        else {
                fd = open(fn, O_RDONLY);
                if (fd < 0) {
                        fprintf(stderr, ME "open error: %s: %s\n", fn,
                                safe_strerror(errno));
                        return -SG_LIB_FILE_ERROR;
                }
        }
        if (sg_set_binary_mode(fd) < 0) {
                perror("sg_set_binary_mode");
                return -SG_LIB_FILE_ERROR;
        }
        return fd;
}

static int
open_dev(const char * outf, int verbose)
{
        int sg_fd = sg_cmds_open_device(outf, 0 /* rw */, verbose);
        if (sg_fd < 0) {
                fprintf(stderr, ME "open error: %s: %s\n", outf,
                        safe_strerror(-sg_fd));
                return -SG_LIB_FILE_ERROR;
        }

        return sg_fd;
}


int
main(int argc, char * argv[])
{
        int res, half_xlen, ifn_stdin;
        int infd = -1;
        int wfd = -1;
        int devfd = -1;
        unsigned char * wrkBuff = NULL;
        struct opts_t opts;
        struct opts_t * op;

        op = &opts;
        memset(op, 0, sizeof(opts));
        res = parse_args(argc, argv, op);
        if (res != 0) {
                fprintf(stderr, "Failed parsing args\n");
                goto out;
        }

        if (op->verbose) {
                fprintf(stderr, "Running COMPARE AND WRITE command with the "
                        "following options:\n  in=%s ", op->ifn);
                if (op->wfn_given)
                        fprintf(stderr, "inw=%s ", op->wfn);
                fprintf(stderr, "device=%s\n  lba=0x%" PRIx64
                        " num_blocks=%d xfer_len=%d timeout=%d\n",
                        op->device_name, op->lba, op->numblocks,
                        op->xfer_len, op->timeout);
        }
        ifn_stdin = ((1 == strlen(op->ifn)) && ('-' == op->ifn[0]));
        infd = open_if(op->ifn, ifn_stdin);
        if (infd < 0) {
                res = -infd;
                goto out;
        }
        if (op->wfn_given) {
                if ((1 == strlen(op->wfn)) && ('-' == op->wfn[0])) {
                        fprintf(stderr, ME "don't allow stdin for write "
                                "file\n");
                        res = SG_LIB_FILE_ERROR;
                        goto out;
                }
                wfd = open_if(op->wfn, 0);
                if (wfd < 0) {
                        res = -wfd;
                        goto out;
                }
        }

        devfd = open_dev(op->device_name, op->verbose);
        if (devfd < 0) {
                res = -devfd;
                goto out;
        }

        wrkBuff = (unsigned char *)malloc(op->xfer_len);
        if (0 == wrkBuff) {
                fprintf(stderr, "Not enough user memory\n");
                res = SG_LIB_CAT_OTHER;
                goto out;
        }

        if (op->wfn_given) {
                half_xlen = op->xfer_len / 2;
                res = read(infd, wrkBuff, half_xlen);
                if (res < 0) {
                        fprintf(stderr, "Could not read from %s", op->ifn);
                        goto out;
                } else if (res < half_xlen) {
                        fprintf(stderr, "Read only %d bytes (expected %d) "
                                "from %s\n", res, half_xlen, op->ifn);
                        goto out;
                }
                res = read(wfd, wrkBuff + half_xlen, half_xlen);
                if (res < 0) {
                        fprintf(stderr, "Could not read from %s", op->wfn);
                        goto out;
                } else if (res < half_xlen) {
                        fprintf(stderr, "Read only %d bytes (expected %d) "
                                "from %s\n", res, half_xlen, op->wfn);
                        goto out;
                }
        } else {
                res = read(infd, wrkBuff, op->xfer_len);
                if (res < 0) {
                        fprintf(stderr, "Could not read from %s", op->ifn);
                        goto out;
                } else if (res < op->xfer_len) {
                        fprintf(stderr, "Read only %d bytes (expected %d) "
                                "from %s\n", res, op->xfer_len, op->ifn);
                        goto out;
                }
        }
        res = sg_compare_and_write(devfd, wrkBuff, op->numblocks, op->lba,
                op->xfer_len, op->flags, !op->quiet, op->verbose);

out:
        if (0 != res) {
                char b[80];

                switch (res) {
                case SG_LIB_CAT_MEDIUM_HARD:
                case SG_LIB_CAT_MISCOMPARE:
                case SG_LIB_FILE_ERROR:
                        break;  /* already reported */
                default:
                        sg_get_category_sense_str(res, sizeof(b), b,
                                                  op->verbose);
                        fprintf(stderr, ME "SCSI COMPARE AND WRITE: %s\n", b);
                        break;
                }
        }

        if (wrkBuff)
                free(wrkBuff);
        if ((infd >= 0) && (! ifn_stdin))
                close(infd);
        if (wfd >= 0)
                close(wfd);
        if (devfd >= 0)
                close(devfd);
        return res;
}
