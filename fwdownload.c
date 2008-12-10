/*
 * Copyright (c) EMC Corporation 2008
 * Copyright (c) Mark Lord 2008
 *
 * You may use/distribute this freely, under the terms of either
 * (your choice) the GNU General Public License version 2,
 * or a BSD style license.
 */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <sys/mman.h>

#include "hdparm.h"
#include "sgio.h"

/* Download a firmware segment to the drive */
static int send_firmware (int fd, unsigned int xfer_mode, unsigned int offset,
			  const void *data, unsigned int bytecount, int final_80h)
{
	int err = 0;
	struct hdio_taskfile *r;
	unsigned int blockcount = bytecount / 512;
	__u64 lba;

	lba = ((offset / 512) << 8) | ((blockcount >> 8) & 0xff);
	r = malloc(sizeof(struct hdio_taskfile) + bytecount);
	if (!r) {
		err = errno;
		perror("malloc()");
		return err;
	}
	init_hdio_taskfile(r, ATA_OP_DOWNLOAD_MICROCODE, RW_WRITE, LBA28_OK, lba, blockcount & 0xff, bytecount);
	r->lob.feat = xfer_mode;

	if (final_80h)
		r->lob.feat |= 0x80;

	r->oflags.b.feat  = 1;
	r->iflags.b.nsect = 1;

	memcpy(r->data, data, bytecount);

	if (do_taskfile_cmd(fd, r, 30 /* timeout in secs */)) {
		err = errno;
		putchar('\n');
		perror("FAILED");
	} else {
		putchar('.');
		fflush(stdout);
		if (xfer_mode == 3) {
			switch (r->lob.nsect) {
				case 1:	// drive wants more data
				case 2:	// drive thinks it is all done
					err = - r->lob.nsect;
					break;
				default: // no status indication
					break;
			}
		}
	}
	free(r);
	return err;
}

static int is_stec_c5240 (__u16 *id)
{
	static const char stec_model[] = "TSCEM CA8HS DS";
	static const char stec_fwrev[] = "5C42-0";

	return (0 == memcmp(id + 27, stec_model, strlen(stec_model))
	 && 0 == memcmp(id + 23, stec_fwrev, strlen(stec_fwrev)));
}

int fwdownload (int fd, __u16 *id, const char *fwpath)
{
	int fwfd, err = 0, final_80h = 0, eof_okay = 0;
	struct stat st;
	const char *fw = NULL;
	const int max_bytes = 0xffff * 512;
	int xfer_mode, xfer_min = 1, xfer_max = 0xffff;
	ssize_t offset;

	if ((fwfd = open(fwpath, O_RDONLY)) == -1 || fstat(fwfd, &st) == -1) {
		err = errno;
		perror(fwpath);
		return err;
	}

	if (!S_ISREG(st.st_mode)) {
		fprintf(stderr, "%s: not a regular file\n", fwpath);
		err = EINVAL;
		goto done;
	}

	if (st.st_size > max_bytes) {
	    	fprintf(stderr, "%s: file size too large, max=%u bytes\n", fwpath, max_bytes);
		err = EINVAL;
		goto done;
	}

	if (st.st_size == 0 || st.st_size % 512) {
	    	fprintf(stderr, "%s: file size (%llu) not a multiple of 512\n",
			fwpath, (unsigned long long) st.st_size);
		err = EINVAL;
		goto done;
	}

	printf("%s: %llu bytes\n", fwpath, (unsigned long long)st.st_size);

	fw = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fwfd, 0);
	if (fw == MAP_FAILED) {
		err = errno;
		perror(fwpath);
		goto done;
	}
	if (-1 == mlock(fw, st.st_size))
		perror("mlock()");

	if (is_stec_c5240(id)) {
		xfer_mode = 3;
		final_80h = 1;
		eof_okay  = 1;
	} else {
		/* Check drive for fwdownload support */
		if (!((id[83] & 1) && (id[86] & 1))) {
			fprintf(stderr, "DOWNLOAD_MICROCODE: not supported by device\n");
			err = ENOTSUP;
			goto done;
		}
		if (((id[119] & 0x10) && (id[120] & 0x10)) || is_stec_c5240(id))
			xfer_mode = 3;
		else
			xfer_mode = 7;
	}
	if (xfer_mode == 3) {
		/* the newer, segmented transfer mode */
		xfer_min = id[234];
		if (xfer_min == 0 || xfer_min == 0xffff)
			xfer_min = 1;
		xfer_max = id[235];
		if (xfer_max == 0 || xfer_max == 0xffff)
			xfer_max = xfer_min;
		fprintf(stderr, "mode=%u, min=%u, max=%u\n", xfer_mode, xfer_min, xfer_max);
	}
	xfer_min *= 512;
	xfer_max *= 512;

	/* Perform the fwdownload, in segments if appropriate */
	for (offset = 0; !err && offset < st.st_size;) {
		int final_chunk = ((offset + xfer_min) >= st.st_size);
		err = send_firmware(fd, xfer_mode, offset, fw + offset, xfer_min, final_chunk && final_80h);
		offset += xfer_min;
		if (err == -2) {	// drive has had enough?
			if (offset >= st.st_size) { // transfer complete?
				err = 0;
			} else {
				fprintf(stderr, "Error: drive completed transfer at %d/%u bytes\n",
							offset, (unsigned int)st.st_size);
				err = EIO;
			}
		} else if (err == -1 && !eof_okay) {
			if (offset >= st.st_size) { // no more data?
				fprintf(stderr, "Error: drive expects more data than provided\n");
				err = EIO;
			} else {
				err = 0;
			}
		}
	}
	if (!err)
		printf(" Done.\n");
done:
	munlock(fw, st.st_size);
	close (fwfd);
	return err;
}
