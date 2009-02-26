#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include "hdparm.h"
/*
 * dvdspeed - use SET STREAMING command to set the speed of DVD-drives
 *
 * Copyright (c) 2004	Thomas Fritzsche <tf@noto.de>
 * A bit mangled in 2006 and 2008 by Thomas Orgis <thomas@orgis.org>
 *
 */

int set_dvdspeed(int fd, int speed)
{
	struct cdrom_generic_command cgc;
	struct request_sense sense;
	unsigned char buffer[28];
	memset(&cgc, 0, sizeof(cgc));
	memset(&sense, 0, sizeof(sense));
	memset(&buffer, 0, sizeof(buffer));

	/* SET STREAMING command */
	cgc.cmd[0] = 0xb6;
	/* 28 byte parameter list length */
	cgc.cmd[10] = 28;
	cgc.sense = &sense;
	cgc.buffer = buffer;
	cgc.buflen = sizeof(buffer);
	cgc.data_direction = CGC_DATA_WRITE;

	buffer[8] = 0xff;
	buffer[9] = 0xff;
	buffer[10] = 0xff;
	buffer[11] = 0xff;

	buffer[15] = 177*speed;
	buffer[18] = 0x03;
	buffer[19] = 0xE8;

	buffer[23] = 177*speed;
	buffer[26] = 0x03;
	buffer[27] = 0xE8;

	if (ioctl(fd, CDROM_SEND_PACKET, &cgc) == 0)
		return 0;
	else
		return -1;
}

