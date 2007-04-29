/* hdparm.c - Command line interface to get/set hard disk parameters */
/*          - by Mark Lord (C) 1994-2007 -- freely distributable */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <endian.h>
#include <sys/ioctl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <linux/types.h>
#include <linux/major.h>
#include <asm/byteorder.h>

#include "hdparm.h"

extern const char *minor_str[];

#define VERSION "v7.1"

#ifndef O_DIRECT
#define O_DIRECT	040000	/* direct disk access, not easily obtained from headers */
#endif

#ifndef CDROM_SELECT_SPEED	/* already defined in 2.3.xx kernels and above */
#define CDROM_SELECT_SPEED	0x5322
#endif

#define TIMING_BUF_MB		2
#define TIMING_BUF_BYTES	(TIMING_BUF_MB * 1024 * 1024)

#ifndef ATA_OP_SECURITY_FREEZE_LOCK
	#define ATA_OP_SECURITY_SET_PASS		0xF1
	#define ATA_OP_SECURITY_UNLOCK		0xF2
	#define ATA_OP_SECURITY_ERASE_PREPARE	0xF3
	#define ATA_OP_SECURITY_ERASE_UNIT		0xF4
	#define ATA_OP_SECURITY_FREEZE_LOCK	0xF5
	#define ATA_OP_SECURITY_DISABLE		0xF6
#endif

char *progname;
int verbose = 0;
static int do_defaults = 0, do_flush = 0, do_ctimings, do_timings = 0;
static int do_identity = 0, get_geom = 0, noisy = 1, quiet = 0;

static int set_fsreadahead= 0, get_fsreadahead= 0, fsreadahead= 0;
static int set_readonly = 0, get_readonly = 0, readonly = 0;
static int set_unmask   = 0, get_unmask   = 0, unmask   = 0;
static int set_mult     = 0, get_mult     = 0, mult     = 0;
static int set_dma      = 0, get_dma      = 0, dma      = 0;
static int set_dma_q	  = 0, get_dma_q    = 0, dma_q	  = 0;
static int set_nowerr   = 0, get_nowerr   = 0, nowerr   = 0;
static int set_keep     = 0, get_keep     = 0, keep     = 0;
static int set_io32bit  = 0, get_io32bit  = 0, io32bit  = 0;
static int set_piomode  = 0, get_piomode= 0, piomode = 0;
static int set_dkeep    = 0, get_dkeep    = 0, dkeep    = 0;
static int set_standby  = 0, get_standby  = 0, standby= 0;
static int set_xfermode = 0, get_xfermode = 0;
static int xfermode_requested= 0;
static int set_lookahead= 0, get_lookahead= 0, lookahead= 0;
static int set_prefetch = 0, get_prefetch = 0, prefetch = 0;
static int set_defects  = 0, get_defects  = 0, defects  = 0;
static int set_wcache   = 0, get_wcache   = 0, wcache   = 0;
static int set_doorlock = 0, get_doorlock = 0, doorlock = 0;
static int set_seagate  = 0, get_seagate  = 0;
static int set_standbynow = 0, get_standbynow = 0;
static int set_sleepnow   = 0, get_sleepnow   = 0;
static int set_powerup_in_standby = 0, get_powerup_in_standby = 0, powerup_in_standby = 0;
static int get_hitachi_temp = 0, set_hitachi_temp = 0;
static int set_freeze   = 0;
static int security_master = 1, security_mode = 0;
static int enhanced_erase = 0;
static int set_security   = 0;
static unsigned int security_command = ATA_OP_SECURITY_UNLOCK;

static char security_password[33];

static int get_powermode  = 0, set_powermode = 0;
static int set_apmmode = 0, get_apmmode= 0, apmmode = 0;
static int get_cdromspeed = 0, set_cdromspeed = 0, cdromspeed = 0;
static int do_IDentity = 0, do_drq_hsm_error = 0;
static int get_unregister = 0, set_unregister = 0, unregister = 0;
static int	hwif = 0;
static int	scan_hwif = 0;
static int	hwif_data = 0;
static int	hwif_ctrl = 0;
static int	hwif_irq = 0;
static int	set_busstate = 0, get_busstate = 0, busstate = 0;
static int	set_reread_partn = 0, get_reread_partn;
static int	set_acoustic = 0, get_acoustic = 0, acoustic = 0;

static int	get_doreset = 0, set_doreset = 0;
static int	get_tristate = 0, set_tristate = 0, tristate = 0;

static int open_flags = O_RDONLY|O_NONBLOCK;

// Historically, if there was no HDIO_OBSOLETE_IDENTITY, then
// then the HDIO_GET_IDENTITY only returned 142 bytes.
// Otherwise, HDIO_OBSOLETE_IDENTITY returns 142 bytes,
// and HDIO_GET_IDENTITY returns 512 bytes.  But the latest
// 2.5.xx kernels no longer define HDIO_OBSOLETE_IDENTITY
// (which they should, but they should just return -EINVAL).
//
// So.. we must now assume that HDIO_GET_IDENTITY returns 512 bytes.
// On a really old system, it will not, and we will be confused.
// Too bad, really.

const char *cfg_str[] =
{	"",	        " HardSect",   " SoftSect",  " NotMFM",
	" HdSw>15uSec", " SpinMotCtl", " Fixed",     " Removeable",
	" DTR<=5Mbs",   " DTR>5Mbs",   " DTR>10Mbs", " RotSpdTol>.5%",
	" dStbOff",     " TrkOff",     " FmtGapReq", " nonMagnetic"
};

const char *SlowMedFast[]	= {"slow", "medium", "fast", "eide", "ata"};
const char *BuffType[]	= {"unknown", "1Sect", "DualPort", "DualPortCache"};

#define YN(b)	(((b)==0)?"no":"yes")

static void dmpstr (const char *prefix, unsigned int i, const char *s[], unsigned int maxi)
{
	if (i > maxi)
		printf("%s%u", prefix, i);
	else
		printf("%s%s", prefix, s[i]);
}

static void dump_identity (__u16 *idw)
{
	int i;
	char pmodes[64] = {0,}, dmodes[128]={0,}, umodes[128]={0,};
	__u8 tPIO;

	printf("\n Model=%.40s, FwRev=%.8s, SerialNo=%.20s",
		(char *)&idw[27], (char *)&idw[23], (char *)&idw[10]);
	printf("\n Config={");
	for (i = 0; i <= 15; i++) {
		if (idw[0] & (1<<i))
			printf("%s", cfg_str[i]);
	}
	printf(" }\n");
	printf(" RawCHS=%u/%u/%u, TrkSize=%u, SectSize=%u, ECCbytes=%u\n",
		idw[1], idw[3], idw[6], idw[4], idw[5], idw[22]);
	dmpstr(" BuffType=", idw[20], BuffType, 3);
	printf(", BuffSize=%ukB, MaxMultSect=%u", idw[21] / 2, idw[47] & 0xff);
	if ((idw[47] & 0xff)) {
		printf(", MultSect=");
		if (!(idw[59] & 0x100))
			printf("?%u?", idw[59] & 0xff);
		else if (idw[59] & 0xff)
			printf("%u", idw[59] & 0xff);
		else
			printf("off");
	}
	putchar('\n');
	tPIO = idw[51] >> 8;
	if (tPIO <= 5) {
		strcat(pmodes, "pio0 ");
		if (tPIO >= 1) strcat(pmodes, "pio1 ");
		if (tPIO >= 2) strcat(pmodes, "pio2 ");
	}
	if (!(idw[53] & 1))
		printf(" (maybe):");
	printf(" CurCHS=%u/%u/%u, CurSects=%u", idw[54], idw[55], idw[56], idw[57] | (idw[58] << 16));
	printf(", LBA=%s", YN(idw[49] & 0x200));
	if (idw[49] & 0x200)
 		printf(", LBAsects=%u", idw[60] | (idw[61] << 16));

	if (idw[49] & 0x100) {
		if (idw[62] | idw[63]) {
			if (idw[62] & 0x100)	strcat(dmodes,"*");
			if (idw[62] & 1)	strcat(dmodes,"sdma0 ");
			if (idw[62] & 0x200)	strcat(dmodes,"*");
			if (idw[62] & 2)	strcat(dmodes,"sdma1 ");
			if (idw[62] & 0x400)	strcat(dmodes,"*");
			if (idw[62] & 4)	strcat(dmodes,"sdma2 ");
			if (idw[62] & 0xf800)	strcat(dmodes,"*");
			if (idw[62] & 0xf8)	strcat(dmodes,"sdma? ");
			if (idw[63] & 0x100)	strcat(dmodes,"*");
			if (idw[63] & 1)	strcat(dmodes,"mdma0 ");
			if (idw[63] & 0x200)	strcat(dmodes,"*");
			if (idw[63] & 2)	strcat(dmodes,"mdma1 ");
			if (idw[63] & 0x400)	strcat(dmodes,"*");
			if (idw[63] & 4)	strcat(dmodes,"mdma2 ");
			if (idw[63] & 0xf800)	strcat(dmodes,"*");
			if (idw[63] & 0xf8)	strcat(dmodes,"mdma? ");
		}
	}
	printf("\n IORDY=");
	if (idw[49] & 0x800)
		printf((idw[49] & 0x400) ? "on/off" : "yes");
	else
		printf("no");
	if ((idw[49] & 0x800) || (idw[53] & 2)) {
		if ((idw[53] & 2)) {
			printf(", tPIO={min:%u,w/IORDY:%u}", idw[67], idw[68]);
			if (idw[64] & 1)	strcat(pmodes, "pio3 ");
			if (idw[64] & 2)	strcat(pmodes, "pio4 ");
			if (idw[64] &~3)	strcat(pmodes, "pio? ");
		}
		if (idw[53] & 4) {
			if (idw[88] & 0x100)	strcat(umodes,"*");
			if (idw[88] & 0x001)	strcat(umodes,"udma0 ");
			if (idw[88] & 0x200)	strcat(umodes,"*");
			if (idw[88] & 0x002)	strcat(umodes,"udma1 ");
			if (idw[88] & 0x400)	strcat(umodes,"*");
			if (idw[88] & 0x004)	strcat(umodes,"udma2 ");
			if (idw[88] & 0x800)	strcat(umodes,"*");
			if (idw[88] & 0x008)	strcat(umodes,"udma3 ");
			if (idw[88] & 0x1000)	strcat(umodes,"*");
			if (idw[88] & 0x010)	strcat(umodes,"udma4 ");
			if (idw[88] & 0x2000)	strcat(umodes,"*");
			if (idw[88] & 0x020)	strcat(umodes,"udma5 ");
			if (idw[93] & 0x2000) {
				if (idw[88] & 0x4000)	strcat(umodes,"*");
				if (idw[88] & 0x0040)	strcat(umodes,"udma6 ");
				if (idw[88] & 0x8000)	strcat(umodes,"*");
				if (idw[88] & 0x0080)	strcat(umodes,"udma7 ");
			}
		}
	}
	if ((idw[49] & 0x100) && (idw[53] & 2))
		printf(", tDMA={min:%u,rec:%u}", idw[65], idw[66]);
	printf("\n PIO modes:  %s", pmodes);
	if (*dmodes)
		printf("\n DMA modes:  %s", dmodes);
	if (*umodes)
		printf("\n UDMA modes: %s", umodes);

	printf("\n AdvancedPM=%s",YN(idw[83]&8));
	if (idw[83] & 8) {
		if (!(idw[86]&8))
			printf(": disabled (255)");
		else if ((idw[91]&0xFF00)!=0x4000)
			printf(": unknown setting");
		else
			printf(": mode=0x%02X (%u)",idw[91]&0xFF,idw[91]&0xFF);
	}
	if (idw[82]&0x20)
		printf(" WriteCache=%s",(idw[85]&0x20) ? "enabled" : "disabled");
	if (idw[81] || idw[80]) {
		printf("\n Drive conforms to: ");
		if (idw[81] <= 31)
			printf("%s: ", minor_str[idw[81]]);
		else
			printf("unknown: ");
		if (idw[80] != 0x0000 &&  /* NOVAL_0 */
		    idw[80] != 0xFFFF) {  /* NOVAL_1 */
			int count = 0;
			for (i=0; i <= 7; i++) {
				if (idw[80] & (1<<i))
					printf("%s%u", count++ ? "," : " ATA/ATAPI-", i);
			}
		}
	}
	printf("\n");
	printf("\n * signifies the current active mode\n");
	printf("\n");
}

void flush_buffer_cache (int fd)
{
	fsync (fd);				/* flush buffers */
	if (ioctl(fd, BLKFLSBUF, NULL))		/* do it again, big time */
		perror("BLKFLSBUF failed");
	if (do_drive_cmd(fd, NULL) && errno != EINVAL)	/* await completion */
		perror("HDIO_DRIVE_CMD(null) (wait for flush complete) failed");
}

int seek_to_zero (int fd)
{
	if (lseek(fd, (off_t) 0, SEEK_SET)) {
		perror("lseek() failed");
		return 1;
	}
	return 0;
}

int read_big_block (int fd, char *buf)
{
	int i, rc;
	if ((rc = read(fd, buf, TIMING_BUF_BYTES)) != TIMING_BUF_BYTES) {
		if (rc) {
			if (rc == -1)
				perror("read() failed");
			else
				fprintf(stderr, "read(%u) returned %u bytes\n", TIMING_BUF_BYTES, rc);
		} else {
			fputs ("read() hit EOF - device too small\n", stderr);
		}
		return 1;
	}
	/* access all sectors of buf to ensure the read fully completed */
	for (i = 0; i < TIMING_BUF_BYTES; i += 512)
		buf[i] &= 1;
	return 0;
}

void time_cache (int fd)
{
	char *buf;
	struct itimerval e1, e2;
	int shmid;
	double elapsed, elapsed2;
	unsigned int iterations, total_MB;

	if ((shmid = shmget(IPC_PRIVATE, TIMING_BUF_BYTES, 0600)) == -1) {
		perror ("could not allocate sharedmem buf");
		return;
	}
	if (shmctl(shmid, SHM_LOCK, NULL) == -1) {
		perror ("could not lock sharedmem buf");
		(void) shmctl(shmid, IPC_RMID, NULL);
		return;
	}
	if ((buf = shmat(shmid, (char *) 0, 0)) == (char *) -1) {
		perror ("could not attach sharedmem buf");
		(void) shmctl(shmid, IPC_RMID, NULL);
		return;
	}
	if (shmctl(shmid, IPC_RMID, NULL) == -1)
		perror ("shmctl(,IPC_RMID,) failed");

	/* Clear out the device request queues & give them time to complete */
	sync();
	sleep(3);

	/*
	 * getitimer() is used rather than gettimeofday() because
	 * it is much more consistent (on my machine, at least).
	 */
	setitimer(ITIMER_REAL, &(struct itimerval){{1000,0},{1000,0}}, NULL);
	if (seek_to_zero (fd)) return;
	if (read_big_block (fd, buf)) return;
	printf(" Timing %scached reads:   ", (open_flags & O_DIRECT) ? "O_DIRECT " : "");
	fflush(stdout);

	/* Clear out the device request queues & give them time to complete */
	sync();
	sleep(1);

	/* Now do the timing */
	iterations = 0;
	getitimer(ITIMER_REAL, &e1);
	do {
		++iterations;
		if (seek_to_zero (fd) || read_big_block (fd, buf))
			goto quit;
		getitimer(ITIMER_REAL, &e2);
		elapsed = (e1.it_value.tv_sec - e2.it_value.tv_sec)
		 + ((e1.it_value.tv_usec - e2.it_value.tv_usec) / 1000000.0);
	} while (elapsed < 2.0);
	total_MB = iterations * TIMING_BUF_MB;

	elapsed = (e1.it_value.tv_sec - e2.it_value.tv_sec)
	 + ((e1.it_value.tv_usec - e2.it_value.tv_usec) / 1000000.0);

	/* Now remove the lseek() and getitimer() overheads from the elapsed time */
	getitimer(ITIMER_REAL, &e1);
	do {
		if (seek_to_zero (fd))
			goto quit;
		getitimer(ITIMER_REAL, &e2);
		elapsed2 = (e1.it_value.tv_sec - e2.it_value.tv_sec)
		 + ((e1.it_value.tv_usec - e2.it_value.tv_usec) / 1000000.0);
	} while (--iterations);

	elapsed -= elapsed2;

	if (total_MB >= elapsed)  /* more than 1MB/s */
		printf("%3u MB in %5.2f seconds = %6.2f MB/sec\n",
			total_MB, elapsed,
			total_MB / elapsed);
	else
		printf("%3u MB in %5.2f seconds = %6.2f kB/sec\n",
			total_MB, elapsed,
			total_MB / elapsed * 1024);

	flush_buffer_cache(fd);
	sleep(1);
quit:
	if (-1 == shmdt(buf))
		perror ("could not detach sharedmem buf");
}

static int do_blkgetsize (int fd, unsigned long long *blksize64)
{
	int		rc;
	unsigned int	blksize32 = 0;

	if (0 == ioctl(fd, BLKGETSIZE64, blksize64)) {	// returns bytes
		*blksize64 /= 512;
		return 0;
	}
	rc = ioctl(fd, BLKGETSIZE, &blksize32);	// returns sectors
	if (rc)
		perror(" BLKGETSIZE failed");
	*blksize64 = blksize32;
	return rc;
}

void time_device (int fd)
{
	char *buf;
	double elapsed;
	struct itimerval e1, e2;
	int shmid;
	unsigned int max_iterations = 1024, total_MB, iterations;

	/*
	 * get device size
	 */
	if (do_ctimings || do_timings) {
		unsigned long long blksize;
		do_flush = 1;
		if (0 == do_blkgetsize(fd, &blksize))
			max_iterations = blksize / (2 * 1024) / TIMING_BUF_MB;
	}

	if ((shmid = shmget(IPC_PRIVATE, TIMING_BUF_BYTES, 0600)) == -1) {
		perror ("could not allocate sharedmem buf");
		return;
	}
	if (shmctl(shmid, SHM_LOCK, NULL) == -1) {
		perror ("could not lock sharedmem buf");
		(void) shmctl(shmid, IPC_RMID, NULL);
		return;
	}
	if ((buf = shmat(shmid, (char *) 0, 0)) == (char *) -1) {
		perror ("could not attach sharedmem buf");
		(void) shmctl(shmid, IPC_RMID, NULL);
		return;
	}
	if (shmctl(shmid, IPC_RMID, NULL) == -1)
		perror ("shmctl(,IPC_RMID,) failed");

	/* Clear out the device request queues & give them time to complete */
	sync();
	sleep(3);

	printf(" Timing %s disk reads:  ", (open_flags & O_DIRECT) ? "O_DIRECT" : "buffered");
	fflush(stdout);

	/*
	 * getitimer() is used rather than gettimeofday() because
	 * it is much more consistent (on my machine, at least).
	 */
	setitimer(ITIMER_REAL, &(struct itimerval){{1000,0},{1000,0}}, NULL);

	/* Now do the timings for real */
	iterations = 0;
	getitimer(ITIMER_REAL, &e1);
	do {
		++iterations;
		if (read_big_block (fd, buf))
			goto quit;
		getitimer(ITIMER_REAL, &e2);
		elapsed = (e1.it_value.tv_sec - e2.it_value.tv_sec)
		 + ((e1.it_value.tv_usec - e2.it_value.tv_usec) / 1000000.0);
	} while (elapsed < 3.0 && iterations < max_iterations);

	total_MB = iterations * TIMING_BUF_MB;
	if ((total_MB / elapsed) > 1.0)  /* more than 1MB/s */
		printf("%3u MB in %5.2f seconds = %6.2f MB/sec\n",
			total_MB, elapsed, total_MB / elapsed);
	else
		printf("%3u MB in %5.2f seconds = %6.2f kB/sec\n",
			total_MB, elapsed, total_MB / elapsed * 1024);
quit:
	if (-1 == shmdt(buf))
		perror ("could not detach sharedmem buf");
}

static void on_off (unsigned int value)
{
	printf(value ? " (on)\n" : " (off)\n");
}

static const char *busstate_str (unsigned int value)
{
	static const char *states[4] = {"off", "on", "tristate", "unknown"};

	if (value > 3)
		value = 3;
	return states[value];
}

static void interpret_standby (void)
{
	printf(" (");
	switch(standby) {
		case 0:		printf("off");
				break;
		case 252:	printf("21 minutes");
				break;
		case 253:	printf("vendor-specific");
				break;
		case 254:	printf("?reserved");
				break;
		case 255:	printf("21 minutes + 15 seconds");
				break;
		default:
			if (standby <= 240) {
				unsigned int secs = standby * 5;
				unsigned int mins = secs / 60;
				secs %= 60;
				if (mins)	  printf("%u minutes", mins);
				if (mins && secs) printf(" + ");
				if (secs)	  printf("%u seconds", secs);
			} else if (standby <= 251) {
				unsigned int mins = (standby - 240) * 30;
				unsigned int hrs  = mins / 60;
				mins %= 60;
				if (hrs)	  printf("%u hours", hrs);
				if (hrs && mins)  printf(" + ");
				if (mins)	  printf("%u minutes", mins);
			} else
				printf("illegal value");
			break;
	}
	printf(")\n");
}

struct xfermode_entry {
	int val;
	const char *name;
};

static const struct xfermode_entry xfermode_table[] = {
	{ 8,    "pio0" },
	{ 9,    "pio1" },
	{ 10,   "pio2" },
	{ 11,   "pio3" },
	{ 12,   "pio4" },
	{ 13,   "pio5" },
	{ 14,   "pio6" },
	{ 15,   "pio7" },
	{ 16,   "sdma0" },
	{ 17,   "sdma1" },
	{ 18,   "sdma2" },
	{ 19,   "sdma3" },
	{ 20,   "sdma4" },
	{ 21,   "sdma5" },
	{ 22,   "sdma6" },
	{ 23,   "sdma7" },
	{ 32,   "mdma0" },
	{ 33,   "mdma1" },
	{ 34,   "mdma2" },
	{ 35,   "mdma3" },
	{ 36,   "mdma4" },
	{ 37,   "mdma5" },
	{ 38,   "mdma6" },
	{ 39,   "mdma7" },
	{ 64,   "udma0" },
	{ 65,   "udma1" },
	{ 66,   "udma2" },
	{ 67,   "udma3" },
	{ 68,   "udma4" },
	{ 69,   "udma5" },
	{ 70,   "udma6" },
	{ 71,   "udma7" },
	{ 0, NULL }
};

static int translate_xfermode(char * name)
{
	const struct xfermode_entry *tmp;
	char *endptr;
	int val = -1;

	for (tmp = xfermode_table; tmp->name != NULL; ++tmp) {
		if (!strcmp(name, tmp->name))
			return tmp->val;
	}
	val = strtol(name, &endptr, 10);
	if (*endptr == '\0')
		return val;
	return -1;
}

static void interpret_xfermode (unsigned int xfermode)
{
	printf(" (");
	switch(xfermode) {
		case 0:		printf("default PIO mode");
				break;
		case 1:		printf("default PIO mode, disable IORDY");
				break;
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:	printf("PIO flow control mode%u", xfermode-8);
				break;
		case 16:
		case 17:
		case 18:
		case 19:
		case 20:
		case 21:
		case 22:
		case 23:	printf("singleword DMA mode%u", xfermode-16);
				break;
		case 32:
		case 33:
		case 34:
		case 35:
		case 36:
		case 37:
		case 38:
		case 39:	printf("multiword DMA mode%u", xfermode-32);
				break;
		case 64:
		case 65:
		case 66:
		case 67:
		case 68:
		case 69:
		case 70:
		case 71:	printf("UltraDMA mode%u", xfermode-64);
				break;
		default:
				printf("unknown, probably not valid");
				break;
	}
	printf(")\n");
}

static void
do_set_security (int fd)
{
	int err = 0;
	const char *description;
	struct hdio_taskfile *r;
	const unsigned int two_hours = 7200, ten_seconds = 10;

	r = malloc(sizeof(struct hdio_taskfile) + 512);
	if (!r) {
		err = errno;
		perror("malloc()");
		exit(err);
	}

	memset(r, 0, sizeof(r));
	r->cmd_req	= TASKFILE_CMD_REQ_OUT;
	r->xfer_method	= TASKFILE_XFER_METHOD_PIO_OUT;
	r->out_bytes	= 512;
	r->lob.command	= security_command;
	r->data[0]	= security_master & 0x01;
	memcpy(&r->data[2], security_password, 32);

	/* Not setting any out_flags causes a segfault and most
	   of the times a kernel panic */
	r->out_flags.lob.command = 1;

	switch (security_command) {
		case ATA_OP_SECURITY_ERASE_UNIT:
			description = "SECURITY_ERASE";
			r->data[0] |= (enhanced_erase & 0x02);
			break;
		case ATA_OP_SECURITY_DISABLE:
			description = "SECURITY_DISABLE";
			break;
		case ATA_OP_SECURITY_UNLOCK:
			description = "SECURITY_UNLOCK";
			break;
		case ATA_OP_SECURITY_SET_PASS:
			description = "SECURITY_SET_PASS";
			r->data[1] = (security_mode & 0x01);
			if (security_master) {
				/* master password revision code */
				r->data[34] = 0x11;
				r->data[35] = 0xff;
			}
			break;
		default:
			fprintf(stderr, "BUG in do_set_security(), command1=0x%x\n", security_command);
			exit(EINVAL);
	}
	printf(" Issuing %s command, password=\"%s\", user=%s",
		description, security_password, r->data[0] ? "master" : "user");
	if (security_command == ATA_OP_SECURITY_SET_PASS)
		printf(", mode=%s", r->data[1] ? "max" : "high");
	printf("\n");

	/*
	 * The Linux kernel IDE driver (until at least 2.6.12) segfaults on the first
	 * command when issued on a locked drive, and the actual erase is never issued.
	 * One could patch the code to issue separate commands for erase prepare and
	 * erase to erase a locked drive.
	 *
	 * We would like to issue these commands consecutively, but since the Linux
	 * kernel until at least 2.6.12 segfaults on each command issued the second will
	 * never be executed.
	 *
	 * One is at least able to issue the commands consecutively in two hdparm invocations,
	 * assuming the segfault isn't followed by an oops.
	 */
	if (security_command == ATA_OP_SECURITY_ERASE_UNIT) {
		__u8 args[4] = {ATA_OP_SECURITY_ERASE_PREPARE,0,0,0};
		if (do_drive_cmd(fd, args)) {
			err = errno;
			perror("ERASE_PREPARE");
		} else {
			if ((do_taskfile_cmd(fd, r, two_hours))) {
				err = errno;
				perror("SECURITY_ERASE");
			}
		}
	} else if (security_command == ATA_OP_SECURITY_DISABLE) {
		/* First attempt an unlock  */
		r->lob.command = ATA_OP_SECURITY_UNLOCK;
		if ((do_taskfile_cmd(fd, r, ten_seconds))) {
			err = errno;
			perror("SECURITY_UNLOCK");
		} else {
			/* Then the security disable */
			r->lob.command = security_command;
			if ((do_taskfile_cmd(fd, r, ten_seconds))) {
				err = errno;
				perror("SECURITY_DISABLE");
			}
		}
	} else if (security_command == ATA_OP_SECURITY_UNLOCK) {
		if ((do_taskfile_cmd(fd, r, ten_seconds))) {
			err = errno;
			perror("SECURITY_UNLOCK");
		}
	} else if (security_command == ATA_OP_SECURITY_SET_PASS) {
		if ((do_taskfile_cmd(fd, r, ten_seconds))) {
			err = errno;
			perror("SECURITY_SET_PASS");
		}
	} else {
		fprintf(stderr, "BUG in do_set_security(), command2=0x%x\n", security_command);
		err = EINVAL;
	}
	free(r);
	if (err)
		exit(err);
}

static __u8 last_identify_op = 0;

static void *get_identify_data (int fd, void *prev)
{
	static __u8 args[4+512];
	__u16 *id = (void *)(args + 4);
	int i;

	if (prev != (void *)-1)
		return prev;
	memset(args, 0, sizeof(args));
	last_identify_op = ATA_OP_IDENTIFY;
	args[0] = last_identify_op;
	args[3] = 1;
	if (do_drive_cmd(fd, args)) {
		last_identify_op = ATA_OP_PIDENTIFY;
		args[0] = last_identify_op;
		args[1] = 0;
		args[2] = 0;
		args[3] = 1;
		if (do_drive_cmd(fd, args)) {
			perror(" HDIO_DRIVE_CMD(identify) failed");
			return NULL;
		}
	}
	/* byte-swap the little-endian IDENTIFY data to match byte-order on host CPU */
	for (i = 0; i < 0x100; ++i)
		__le16_to_cpus(&id[i]);
	return id;
}

void process_dev (char *devname)
{
	int fd;
	static long parm, multcount;
	__u16 *id = (void *)-1;

	fd = open (devname, open_flags);
	if (fd < 0) {
		int err = errno;
		perror(devname);
		exit(err);
	}
	if (!quiet)
		printf("\n%s:\n", devname);

	if (set_fsreadahead) {
		if (get_fsreadahead)
			printf(" setting fs readahead to %d\n", fsreadahead);
		if (ioctl(fd, BLKRASET, fsreadahead))
			perror(" BLKRASET failed");
	}
	if (set_unregister) {
		if (get_unregister)
			printf(" attempting to unregister hwif#%u\n", hwif);
		if (ioctl(fd, HDIO_UNREGISTER_HWIF, hwif))
			perror(" HDIO_UNREGISTER_HWIF failed");
	}
	if (scan_hwif) {
		int	args[3];
		printf(" attempting to scan hwif (0x%x, 0x%x, %u)\n", hwif_data, hwif_ctrl, hwif_irq);
		args[0] = hwif_data;
		args[1] = hwif_ctrl;
		args[2] = hwif_irq;
		if (ioctl(fd, HDIO_SCAN_HWIF, args))
			perror(" HDIO_SCAN_HWIF failed");
	}
	if (set_piomode) {
		if (get_piomode) {
			if (piomode == 255)
				printf(" attempting to auto-tune PIO mode\n");
			else if (piomode < 100)
				printf(" attempting to set PIO mode to %d\n", piomode);
			else if (piomode < 200)
				printf(" attempting to set MDMA mode to %d\n", (piomode-100));
			else
				printf(" attempting to set UDMA mode to %d\n", (piomode-200));
		}
		if (ioctl(fd, HDIO_SET_PIO_MODE, piomode))
			perror(" HDIO_SET_PIO_MODE failed");
	}
	if (set_io32bit) {
		if (get_io32bit)
			printf(" setting 32-bit IO_support flag to %d\n", io32bit);
		if (ioctl(fd, HDIO_SET_32BIT, io32bit))
			perror(" HDIO_SET_32BIT failed");
	}
	if (set_mult) {
		if (get_mult)
			printf(" setting multcount to %d\n", mult);
		if (ioctl(fd, HDIO_SET_MULTCOUNT, mult))
			perror(" HDIO_SET_MULTCOUNT failed");
	}
	if (set_readonly) {
		if (get_readonly) {
			printf(" setting readonly to %d", readonly);
			on_off(readonly);
		}
		if (ioctl(fd, BLKROSET, &readonly))
			perror(" BLKROSET failed");
	}
	if (set_unmask) {
		if (get_unmask) {
			printf(" setting unmaskirq to %d", unmask);
			on_off(unmask);
		}
		if (ioctl(fd, HDIO_SET_UNMASKINTR, unmask))
			perror(" HDIO_SET_UNMASKINTR failed");
	}
	if (set_dma) {
		if (get_dma) {
			printf(" setting using_dma to %d", dma);
			on_off(dma);
		}
		if (ioctl(fd, HDIO_SET_DMA, dma))
			perror(" HDIO_SET_DMA failed");
	}
	if (set_dma_q) {
		if (get_dma_q) {
			printf(" setting DMA queue_depth to %d", dma_q);
			on_off(dma_q);
		}
		if (ioctl(fd, HDIO_SET_QDMA, dma_q))
			perror(" HDIO_SET_QDMA failed");
	}
	if (set_nowerr) {
		if (get_nowerr) {
			printf(" setting nowerr to %d", nowerr);
			on_off(nowerr);
		}
		if (ioctl(fd, HDIO_SET_NOWERR, nowerr))
			perror(" HDIO_SET_NOWERR failed");
	}
	if (set_keep) {
		if (get_keep) {
			printf(" setting keep_settings to %d", keep);
			on_off(keep);
		}
		if (ioctl(fd, HDIO_SET_KEEPSETTINGS, keep))
			perror(" HDIO_SET_KEEPSETTINGS failed");
	}
	if (set_doorlock) {
		__u8 args[4] = {0,0,0,0};
		args[0] = doorlock ? ATA_OP_DOORLOCK : ATA_OP_DOORUNLOCK;
		if (get_doorlock) {
			printf(" setting drive doorlock to %d", doorlock);
			on_off(doorlock);
		}
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(doorlock) failed");
	}
	if (set_dkeep) {
		/* lock/unlock the drive's "feature" settings */
		__u8 args[4] = {ATA_OP_SETFEATURES,0,0,0};
		if (get_dkeep) {
			printf(" setting drive keep features to %d", dkeep);
			on_off(dkeep);
		}
		args[2] = dkeep ? 0x66 : 0xcc;
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(keepsettings) failed");
	}
	if (set_defects) {
		__u8 args[4] = {ATA_OP_SETFEATURES,0,0x04,0};
		args[2] = defects ? 0x04 : 0x84;
		if (get_defects)
			printf(" setting drive defect management to %d\n", defects);
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(defectmgmt) failed");
	}
	if (set_prefetch) {
		__u8 args[4] = {ATA_OP_SETFEATURES,0,0xab,0};
		args[1] = prefetch;
		if (get_prefetch)
			printf(" setting drive prefetch to %d\n", prefetch);
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(setprefetch) failed");
	}
	if (set_xfermode) {
		__u8 args[4] = {ATA_OP_SETFEATURES,0,3,0};
		args[1] = xfermode_requested;
		if (get_xfermode) {
			printf(" setting xfermode to %d", xfermode_requested);
			interpret_xfermode(xfermode_requested);
		}
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(setxfermode) failed");
	}
	if (set_lookahead) {
		__u8 args[4] = {ATA_OP_SETFEATURES,0,0,0};
		args[2] = lookahead ? 0xaa : 0x55;
		if (get_lookahead) {
			printf(" setting drive read-lookahead to %d", lookahead);
			on_off(lookahead);
		}
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(setreadahead) failed");
	}
	if (set_powerup_in_standby) {
		__u8 args[4] = {ATA_OP_SETFEATURES,0,0,0};
		if (powerup_in_standby == 0) {
			__u8 args1[4] = {ATA_OP_SETFEATURES,0,0x07,0}; /* spinup from standby */
			printf(" spin-up:");
			fflush(stdout);
			(void) do_drive_cmd(fd, args1);
		}
		if (get_powerup_in_standby) {
			printf(" setting power-up in standby to %d", powerup_in_standby);
			fflush(stdout);
			on_off(powerup_in_standby);
		}
		args[0] = ATA_OP_SETFEATURES;
		args[2] = powerup_in_standby ? 0x06 : 0x86;
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(powerup_in_standby) failed");
	}
	if (set_apmmode) {
		__u8 args[4] = {ATA_OP_SETFEATURES,0,0,0};
		if (get_apmmode)
			printf(" setting Advanced Power Management level to");
		if (apmmode==255) {
			/* disable Advanced Power Management */
			args[2] = 0x85; /* feature register */
			if (get_apmmode) printf(" disabled\n");
		} else {
			/* set Advanced Power Management mode */
			args[2] = 0x05; /* feature register */
			args[1] = apmmode; /* sector count register */
			if (get_apmmode)
				printf(" 0x%02x (%d)\n",apmmode,apmmode);
		}
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD failed");
	}
	if (set_cdromspeed) {
		if (get_cdromspeed)
			printf ("setting cdrom speed to %d\n", cdromspeed);
		if (ioctl (fd, CDROM_SELECT_SPEED, cdromspeed))
			perror(" CDROM_SELECT_SPEED failed");
	}
	if (set_acoustic) {
		__u8 args[4];
		if (get_acoustic)
			printf(" setting acoustic management to %d\n", acoustic);
		args[0] = ATA_OP_SETFEATURES;
		args[1] = acoustic;
		args[2] = acoustic ? 0x42 : 0xc2;
		args[3] = 0;
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD:ACOUSTIC failed");
	}
	if (set_wcache) {
		__u8 flushcache1[4] = {ATA_OP_FLUSHCACHE,0,0,0};
		__u8 flushcache2[4] = {ATA_OP_FLUSHCACHE,0,0,0};
		__u8 setcache   [4] = {ATA_OP_SETFEATURES,0,0,0};
		setcache[2] = wcache ? 0x02 : 0x82;
		if (get_wcache) {
			printf(" setting drive write-caching to %d", wcache);
			on_off(wcache);
		}
		if (!wcache && do_drive_cmd(fd, flushcache1))
			perror (" HDIO_DRIVE_CMD(flushcache1) failed");
		if (ioctl(fd, HDIO_SET_WCACHE, wcache))
			if (do_drive_cmd(fd, setcache))
				perror(" HDIO_DRIVE_CMD(setcache) failed");
		if (!wcache && do_drive_cmd(fd, flushcache2))
			perror (" HDIO_DRIVE_CMD(flushcache2) failed");
	}
	if (set_standbynow) {
		__u8 args1[4] = {ATA_OP_STANDBYNOW1,0,0,0};
		__u8 args2[4] = {ATA_OP_STANDBYNOW2,0,0,0};
		if (get_standbynow)
			printf(" issuing standby command\n");
		if (do_drive_cmd(fd, args1)
		 && do_drive_cmd(fd, args2))
			perror(" HDIO_DRIVE_CMD(standby) failed");
	}
	if (set_sleepnow) {
		__u8 args1[4] = {ATA_OP_SLEEPNOW1,0,0,0};
		__u8 args2[4] = {ATA_OP_SLEEPNOW2,0,0,0};
		if (get_sleepnow)
			printf(" issuing sleep command\n");
		if (do_drive_cmd(fd, args1)
		 && do_drive_cmd(fd, args2))
			perror(" HDIO_DRIVE_CMD(sleep) failed");
	}
	if (set_security) {
		do_set_security(fd);
	}
	if (set_freeze) {
		__u8 args[4] = {ATA_OP_SECURITY_FREEZE_LOCK,0,0,0};
		printf(" issuing Security Freeze command\n");
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(freeze) failed");
	}
	if (set_seagate) {
		__u8 args[4] = {0xfb,0,0,0};
		if (get_seagate)
			printf(" disabling Seagate auto powersaving mode\n");
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(seagatepwrsave) failed");
	}
	if (set_standby) {
		__u8 args[4] = {ATA_OP_SETIDLE1,standby,0,0};
		if (get_standby) {
			printf(" setting standby to %u", standby);
			interpret_standby();
		}
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(setidle1) failed");
	}
	if (set_busstate) {
		if (get_busstate)
			printf(" setting bus state to %d (%s)\n", busstate, busstate_str(busstate));
		if (ioctl(fd, HDIO_SET_BUSSTATE, busstate))
			perror(" HDIO_SET_BUSSTATE failed");
	}
	if (do_drq_hsm_error) {
		id = get_identify_data(fd, id);
		if (id) {
			__u8 args[4] = {0,0,0,0};
			args[0] = last_identify_op;
			sync();
			printf(" triggering \"stuck DRQ\" host state machine error\n");
			sync();
			sleep(1);
			do_drive_cmd(fd, args);
			perror("do_drq_hsm_error");
			fprintf(stderr, "ata status=0x%02x ata error=0x%02x\n", args[0], args[1]);
		}
	}
	if (get_hitachi_temp) {
		__u8 args[4] = {0xf0,0,0x01,0}; /* "Sense Condition", vendor-specific */
		if (do_drive_cmd(fd, args))
			perror(" HDIO_DRIVE_CMD(hitachisensecondition) failed");
		else {
			printf(" drive temperature (celsius) is:  ");
			if (args[2]==0)
				printf("under -20");
			else if (args[2]==0xFF)
				printf("over 107");
			else
				printf("%d", args[2]/2-20);
			printf("\n drive temperature in range:  %s\n", YN(!(args[1]&0x10)) );
		}
	}
	if (do_defaults || get_mult || do_identity) {
		multcount = -1;
		if (ioctl(fd, HDIO_GET_MULTCOUNT, &multcount)) {
			if (get_mult)
				perror(" HDIO_GET_MULTCOUNT failed");
		} else if (do_defaults || get_mult) {
			printf(" multcount     = %2ld", multcount);
			on_off(multcount);
		}
	}
	if (do_defaults || get_io32bit) {
		if (0 == ioctl(fd, HDIO_GET_32BIT, &parm)) {
			printf(" IO_support    =%3ld (", parm);
			switch (parm) {
				case 0:	printf("default ");
				case 2: printf("16-bit)\n");
					break;
				case 1:	printf("32-bit)\n");
					break;
				case 3:	printf("32-bit w/sync)\n");
					break;
				case 8:	printf("Request-Queue-Bypass)\n");
					break;
				default:printf("\?\?\?)\n");
			}
		}
	}
	if (do_defaults || get_unmask) {
		if (0 == ioctl(fd, HDIO_GET_UNMASKINTR, &parm)) {
			printf(" unmaskirq     = %2ld", parm);
			on_off(parm);
		}
	}

	if (do_defaults || get_dma) {
		if (0 == ioctl(fd, HDIO_GET_DMA, &parm)) {
			printf(" using_dma     = %2ld", parm);
			if (parm == 8)
				printf(" (DMA-Assisted-PIO)\n");
			else
				on_off(parm);
		}
	}
	if (get_dma_q) {
		if(ioctl(fd, HDIO_GET_QDMA, &parm)) {
			perror(" HDIO_GET_QDMA failed");
		} else {
			printf(" queue_depth   = %2ld", parm);
			on_off(parm);
		}
	}
	if (do_defaults || get_keep) {
		if (0 == ioctl(fd, HDIO_GET_KEEPSETTINGS, &parm)) {
			printf(" keepsettings  = %2ld", parm);
			on_off(parm);
		}
	}
	if (get_nowerr) {
		if (ioctl(fd, HDIO_GET_NOWERR, &parm))
			perror(" HDIO_GET_NOWERR failed");
		else {
			printf(" nowerr        = %2ld", parm);
			on_off(parm);
		}
	}
	if (do_defaults || get_readonly) {
		if (ioctl(fd, BLKROGET, &parm))
			perror(" BLKROGET failed");
		else {
			printf(" readonly      = %2ld", parm);
			on_off(parm);
		}
	}
	if (do_defaults || get_fsreadahead) {
		if (ioctl(fd, BLKRAGET, &parm))
			perror(" BLKRAGET failed");
		else {
			printf(" readahead     = %2ld", parm);
			on_off(parm);
		}
	}
	if (do_defaults || get_geom) {
		unsigned long long blksize;
		static const char msg[] = " geometry      = %u/%u/%u, sectors = %lld, start = %ld\n";
/*
 * Note to self:  when compiled 32-bit (AMD,Mips64), the userspace version of this struct
 * is going to be 32-bits smaller than the kernel representation.. random stack corruption!
 */
		static struct hd_geometry g;
		static struct local_hd_big_geometry bg;

		if (0 == do_blkgetsize(fd, &blksize)) {
			if (!ioctl(fd, HDIO_GETGEO_BIG, &bg))
				printf(msg, bg.cylinders, bg.heads, bg.sectors, blksize, bg.start);
			else if (ioctl(fd, HDIO_GETGEO, &g))
				perror(" HDIO_GETGEO failed");
			else
				printf(msg, g.cylinders, g.heads, g.sectors, blksize, g.start);
		}
	}
	if (get_powermode) {
		__u8 args[4] = {ATA_OP_CHECKPOWERMODE1,0,0,0};
		const char *state;
		if (do_drive_cmd(fd, args)
		 && (args[0] = ATA_OP_CHECKPOWERMODE2) /* (single =) try again with 0x98 */
		 && do_drive_cmd(fd, args))
			state = "unknown";
		else
			state = (args[2] == 255) ? "active/idle" : "standby";
		printf(" drive state is:  %s\n", state);
	}
	if (do_identity) {
		__u16 id2[256];

		if (!ioctl(fd, HDIO_GET_IDENTITY, id2)) {
			if (multcount != -1) {
				id2[59] = multcount | 0x100;
			} else
				id2[59] &= ~0x100;
			dump_identity(id2);
		} else if (errno == -ENOMSG)
			printf(" no identification info available\n");
		else
			perror(" HDIO_GET_IDENTITY failed");
	}
	if (do_IDentity) {
		id = get_identify_data(fd, id);
		if (id) {
			int i;
			if (do_IDentity == 2) {
				for (i = 0; i < (256/8); ++i) {
					printf("%04x %04x %04x %04x %04x %04x %04x %04x\n", id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
					id += 8;
				}
			} else {
				identify((void *)id);
			}
		}
	}
	if (get_lookahead) {
		id = get_identify_data(fd, id);
		if (id) {
			int supported = id[82] & 0x0040;
			if (supported) {
				lookahead = !!(id[85] & 0x0040);
				printf(" look-ahead    = %2d", lookahead);
				on_off(lookahead);
			} else {
				printf(" look-ahead    = not supported\n");
			}
		}
	}
	if (get_wcache) {
		id = get_identify_data(fd, id);
		if (id) {
			int supported = id[82] & 0x0020;
			if (supported) {
				wcache = !!(id[85] & 0x0020);
				printf(" write-caching = %2d", wcache);
				on_off(wcache);
			} else {
				printf(" write-caching = not supported\n");
			}
		}
	}
	if (get_acoustic) {
		id = get_identify_data(fd, id);
		if (id) {
			int supported = id[83] & 0x200;
			if (supported) 
				printf(" acoustic      = %2u (128=quiet ... 254=fast)\n", id[94] & 0xff);
			else
				printf(" acoustic      = not supported\n");
		}
	}
	if (get_busstate) {
		if (ioctl(fd, HDIO_GET_BUSSTATE, &parm))
			perror(" HDIO_GET_BUSSTATE failed");
		else
			printf(" busstate      = %2ld (%s)\n", parm, busstate_str(parm));
	}

	if (do_ctimings)
		time_cache (fd);
	if (do_timings)
		time_device (fd);
	if (do_flush)
		flush_buffer_cache (fd);
	if (set_reread_partn) {
		if (get_reread_partn)
			printf(" re-reading partition table\n");
		if (ioctl(fd, BLKRRPART, NULL)) {
			perror(" BLKRRPART failed");
		}
	}
	if (set_doreset) {
		if (get_doreset)
			printf(" resetting drive\n");
		if (ioctl(fd, HDIO_DRIVE_RESET, NULL))
			perror(" HDIO_DRIVE_RESET failed");
	}
	if (set_tristate) {
		__u8 args[4] = {0,tristate,0,0};
		if (get_tristate)
			printf(" setting tri-state to %d\n", tristate);
		if (ioctl(fd, HDIO_TRISTATE_HWIF, &args))
			perror(" HDIO_TRISTATE_HWIF failed");
	}
	close (fd);
}

static void usage_help (int rc)
{
	FILE *desc = rc ? stderr : stdout;

	fprintf(desc,"\n%s - get/set hard disk parameters - version %s\n\n", progname, VERSION);
	fprintf(desc,"Usage:  %s  [options] [device] ..\n\n", progname);
	fprintf(desc,"Options:\n"
	" -a   get/set fs readahead\n"
	" -A   get/set the drive look-ahead flag (0/1)\n"
	" -b   get/set bus state (0 == off, 1 == on, 2 == tristate)\n"
	" -B   set Advanced Power Management setting (1-255)\n"
	" -c   get/set IDE 32-bit IO setting\n"
	" -C   check drive power mode status\n"
	" -d   get/set using_dma flag\n"
	" -D   enable/disable drive defect management\n"
	" -E   set cd-rom drive speed\n"
	" -f   flush buffer cache for device on exit\n"
	" -g   display drive geometry\n"
	" -h   display terse usage information\n"
	" -H   read temperature from drive (Hitachi only)\n"
	" -i   display drive identification\n"
	" -I   detailed/current information directly from drive\n"
	" -k   get/set keep_settings_over_reset flag (0/1)\n"
	" -K   set drive keep_features_over_reset flag (0/1)\n"
	" -L   set drive doorlock (0/1) (removable harddisks only)\n"
	" -M   get/set acoustic management (0-254, 128: quiet, 254: fast)\n"
	" -m   get/set multiple sector count\n"
	" -n   get/set ignore-write-errors flag (0/1)\n"
	" -p   set PIO mode on IDE interface chipset (0,1,2,3,4,...)\n"
	" -P   set drive prefetch count\n"
	" -q   change next setting quietly\n"
	" -Q   get/set DMA tagged-queuing depth (if supported)\n"
	" -r   get/set device  readonly flag (DANGEROUS to set)\n"
	" -R   register an IDE interface (DANGEROUS)\n"
	" -s   set power-up in standby flag (0/1) (DANGEROUS)\n"
	" -S   set standby (spindown) timeout\n"
	" -t   perform device read timings\n"
	" -T   perform cache read timings\n"
	" -u   get/set unmaskirq flag (0/1)\n"
	" -U   un-register an IDE interface (DANGEROUS)\n"
	" -v   defaults; same as -acdgkmur for IDE drives\n"
	" -V   display program version and exit immediately\n"
	" -w   perform device reset (DANGEROUS)\n"
	" -W   get/set drive write-caching flag (0/1)\n"
	" -x   tristate device for hotswap (0/1) (DANGEROUS)\n"
	" -X   set IDE xfer mode (DANGEROUS)\n"
	" -y   put drive in standby mode\n"
	" -Y   put drive to sleep\n"
	" -Z   disable Seagate auto-powersaving mode\n"
	" -z   re-read partition table\n"
	" --direct         use O_DIRECT to bypass page cache for timings\n"
	" --Istdin         read identify data from stdin as ASCII hex\n"
	" --Istdout        write identify data to stdout as ASCII hex\n"
	" --verbose        display extra diagnostics from some commands\n"
	" --security-help  display help for ATA security commands\n"
	" --drq_hsm_error  crash system with a \"stuck DRQ\" error (VERY DANGEROUS)\n"
	"\n");
	exit(rc);
}

static void security_help (int rc)
{
	FILE *desc = rc ? stderr : stdout;

	fprintf(desc, "\n"
	"ATA Security Commands:\n"
	" Most of these are VERY DANGEROUS and can KILL your drive!\n"
	" Due to bugs in most Linux kernels, use of these commands may even\n"
	" trigger kernel segfaults or worse.  EXPERIMENT AT YOUR OWN RISK!\n"
	"\n"
	" --security-freeze           Freeze security settings until reset.\n"
	"\n"
	" --security-set-pass PASSWD  Lock drive, using password PASSWD:\n"
	"                                  Use 'NULL' to set empty password.\n"
	"                                  Drive gets locked if user-passwd is selected.\n"
	" --security-unlock   PASSWD  Unlock drive.\n"
	" --security-disable  PASSWD  Disable drive locking.\n"
	" --security-erase    PASSWD  Erase a (locked) drive.\n"
	" --security-erase-enhanced PASSWD   Enhanced-erase a (locked) drive.\n"
	"\n"
	" The above four commands may optionally be preceeded by these options:\n"
	" --security-mode  LEVEL      Use LEVEL to select security level:\n"
	"                                  h   high security (default).\n"
	"                                  m   maximum security.\n"
	" --user-master    WHICH      Use WHICH to choose password type:\n"
	"                                  u   user-password.\n"
	"                                  m   master-password (default).\n"
	);
	exit(rc);
}

#define GET_XFERMODE(flag, num)					\
	do {							\
		char *tmpstr = name;				\
		tmpstr[0] = '\0';				\
		if (!*argp && argc && isalnum(**argv))		\
			argp = *argv++, --argc;			\
		while (isalnum(*argp) && (tmpstr - name) < 31) {\
			tmpstr[0] = *argp++;			\
			tmpstr[1] = '\0';			\
			++tmpstr;				\
		}						\
		num = translate_xfermode(name);			\
		if (num == -1)					\
			flag = 0;				\
		else						\
			flag = 1;				\
	} while (0)

static int fromhex (unsigned char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');
	fprintf(stderr, "bad char: '%c' 0x%02x\n", c, c);
	exit(EINVAL);
}

static int ishex (char c)
{
	return ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

static void
identify_from_stdin (void)
{
	__u16 sbuf[512];
	int err, wc = 0;

	do {
		int digit;
		int d[4];

		if (ishex(d[digit=0] = getchar())
		 && ishex(d[++digit] = getchar())
		 && ishex(d[++digit] = getchar())
		 && ishex(d[++digit] = getchar())) {
		 	sbuf[wc] = (fromhex(d[0]) << 12) | (fromhex(d[1]) << 8) | (fromhex(d[2]) << 4) | fromhex(d[3]);
			++wc;
		} else if (d[digit] == EOF) {
			goto eof;
		} else if (wc == 0) {
			/* skip over leading lines of cruft */
			while (d[digit] != '\n') {
				if (d[digit] == EOF)
					goto eof;
				d[digit=0] = getchar();
			};
		}
	} while (wc < 256);
	putchar('\n');
	identify(sbuf);
	return;
eof:
	err = errno;
	fprintf(stderr, "read only %u/256 IDENTIFY words from stdin: %s\n", wc, strerror(err));
	exit(err);
}

static int    argc;
static char **argv;
static char  *argp;
static int    num_flags_processed = 0;

static void
numeric_parm (char c, const char *name, int *val, int *setparm, int *getparm, int min, int max, int set_only)
{
	int got_digit = 0;

	*val = 0;
	*getparm = noisy;
	noisy = 1;
	if (!*argp && argc && isdigit(**argv))
		argp = *argv++, --argc;
	while (isdigit(*argp)) {
		*setparm = 1;
		*val = (*val * 10) + (*argp++ - '0');
		got_digit = 1;
	}
	if ((set_only && !got_digit) || *val < min || *val > max) {
		*val = min;	//FIXME?
		fprintf(stderr, "  -%c: bad/missing %s value (%d..%d)\n", c, name, min, max);
		exit(EINVAL);
	}
}

#define NUMERIC_PARM(CH,NAME,VAR,MIN,MAX,GETSET) numeric_parm(CH,NAME,&VAR,&set_##VAR,&get_##VAR,MIN,MAX,GETSET)
#define GET_SET_PARM(CH,NAME,VAR,MIN,MAX) CH:NUMERIC_PARM(CH,NAME,VAR,MIN,MAX,0);break
#define     SET_PARM(CH,NAME,VAR,MIN,MAX) CH:NUMERIC_PARM(CH,NAME,VAR,MIN,MAX,1);break
#define     SET_FLAG(CH,VAR)              CH:get_##VAR=noisy;noisy=1;set_##VAR=1;break
#define      DO_FLAG(CH,VAR)              CH:VAR=1;noisy=1;break
#define    INCR_FLAG(CH,VAR)              CH:VAR++;noisy=1;break

static void get_security_password (int handle_NULL)
{
	unsigned int maxlen = sizeof(security_password) - 1;

	if (argc < 2) {
		fprintf(stderr, "missing PASSWD\n");
		exit(EINVAL);
	}
	argp = *argv++, --argc;
	if (!argp) {
		fprintf(stderr, "missing PASSWD\n");
		exit(EINVAL);
	}
	if (strlen(argp) > maxlen) {
		fprintf(stderr, "PASSWD too long (must be %d chars max)\n", maxlen);
		exit(EINVAL);
	}
	memset(security_password, 0, maxlen + 1);
	if (!handle_NULL || strcmp(argp, "NULL"))
		strcpy(security_password, argp);
	printf("security_password=\"%s\"\n", security_password);
	while (*argp)
		++argp;
}

static void
handle_standalone_longarg (char *name)
{
	if (num_flags_processed) {
		if (verbose)
			fprintf(stderr, "handle_standalone_longarg: num_flags_processed == %d\n", num_flags_processed);
		usage_help(EINVAL);
	}
	/* --Istdin is special: no filename arg(s) wanted here */
	if (0 == strcasecmp(name, "Istdin")) {
		if (argc > 0) {
			if (verbose)
				fprintf(stderr, "handle_standalone_longarg: argc(%d) > 0\n", argc);
			usage_help(EINVAL);
		}
		identify_from_stdin();
		exit(0);
	}
	if (0 == strcasecmp(name, "security-help")) {
		security_help(0);
		exit(0);
	} else if (0 == strcasecmp(name, "security-freeze")) {
		set_freeze = 1;
	} else if (0 == strcasecmp(name, "security-unlock")) {
		open_flags |= O_RDWR;
		set_security = 1;
		security_command = ATA_OP_SECURITY_UNLOCK;
		get_security_password(0);
	} else if (0 == strcasecmp(name, "security-set-pass")) {
		open_flags |= O_RDWR;
		set_security = 1;
		security_command = ATA_OP_SECURITY_SET_PASS;
		get_security_password(1);
	} else if (0 == strcasecmp(name, "security-disable")) {
		open_flags |= O_RDWR;
		set_security = 1;
		security_command = ATA_OP_SECURITY_DISABLE;
		get_security_password(1);
	} else if (0 == strcasecmp(name, "security-erase")) {
		open_flags |= O_RDWR;
		set_security = 1;
		security_command = ATA_OP_SECURITY_ERASE_UNIT;
		get_security_password(1);
	} else if (0 == strcasecmp(name, "security-erase-enhanced")) {
		open_flags |= O_RDWR;
		set_security = 1;
		enhanced_erase = 1;
		security_command = ATA_OP_SECURITY_ERASE_UNIT;
		get_security_password(1);
	} else {
		usage_help(EINVAL);
	}
}

static int
get_longarg (void)
{
	char *name = argp;

	while (*argp)
		++argp;
	if (0 == strcasecmp(name, "verbose")) {
		verbose = 1;
		--num_flags_processed;	/* doesn't count as an action flag */
	} else if (0 == strcasecmp(name, "direct")) {
		open_flags |= O_DIRECT;
	} else if (0 == strcasecmp(name, "drq-hsm-error")) {
		do_drq_hsm_error = 1;
	} else if (0 == strcasecmp(name, "Istdout")) {
		do_IDentity = 2;
	} else if (0 == strcasecmp(name, "security-mode")) {
		if (argc && isalpha(**argv)) {
			argp = *argv++, --argc;
			if (*argp == 'm')	/* max */
				security_mode = 1;
			else if (*argp == 'h')	/* high */
				security_mode = 0;
			else
				security_help(EINVAL);
			while (*argp) ++argp;
		}
		--num_flags_processed;	/* doesn't count as an action flag */
	} else if (0 == strcasecmp(name, "user-master")) {
		if (argc && isalpha(**argv)) {
			argp = *argv++, --argc;
			if (*argp == 'u')	/* user */
				security_master = 0;
			else if (*argp == 'm')	/* master */
				security_master = 1;
			else
				security_help(EINVAL);
			while (*argp) ++argp;
		}
		--num_flags_processed;	/* doesn't count as an action flag */
	} else {
		handle_standalone_longarg(name);
		return 1; /* 1 == no more flags allowed */
	}
	return 0; /* additional flags allowed */
}

int main (int _argc, char **_argv)
{
	int no_more_flags = 0, disallow_flags = 0;
	char c;
	char name[32];

	argc = _argc;
	argv = _argv;
	argp = NULL;

	if  ((progname = (char *) strrchr(*argv, '/')) == NULL)
		progname = *argv;
	else
		progname++;
	++argv;

	if (!--argc)
		usage_help(EINVAL);
	while (argc--) {
		argp = *argv++;
		if (no_more_flags || argp[0] != '-') {
			if (!num_flags_processed)
				do_defaults = 1;
			process_dev(argp);
			continue;
		}
		if (0 == strcmp(argp, "--")) {
			no_more_flags = 1;
			continue;
		}
		if (disallow_flags)
			usage_help(EINVAL);
		if (!*++argp)
			usage_help(EINVAL);
		while (argp && (c = *argp++)) {
			switch (c) {
				case GET_SET_PARM('a',"filesystem-read-ahead",fsreadahead,0,2048);
				case GET_SET_PARM('A',"look-ahead",lookahead,0,1);
				case GET_SET_PARM('b',"bus-state",busstate,0,2);
				case     SET_PARM('B',"power-management-mode",apmmode,1,255);
				case GET_SET_PARM('c',"32-bit-IO",io32bit,0,3);
				case     SET_FLAG('C',powermode);
				case GET_SET_PARM('d',"dma-enable",dma,0,1);
				case     SET_PARM('D',"defects-management",defects,0,1);
				case     SET_PARM('E',"CDROM/DVD-speed",cdromspeed,0,255);
				case      DO_FLAG('f',do_flush);
				case      DO_FLAG('g',get_geom);
				case              'h': usage_help(0); break;
				case     SET_FLAG('H',hitachi_temp);
				case      DO_FLAG('i',do_identity);
				case      DO_FLAG('I',do_IDentity);
				case GET_SET_PARM('k',"kernel-keep-settings",keep,0,1);
				case     SET_PARM('K',"drive-keep-settings",dkeep,0,1);
				case     SET_PARM('L',"door-lock",doorlock,0,1);
				case GET_SET_PARM('m',"multmode-count",mult,0,64);
				case GET_SET_PARM('M',"acoustic-management",acoustic,0,255);
				case GET_SET_PARM('n',"ignore-write-errors",nowerr,0,1);
				case     SET_PARM('P',"prefetch",prefetch,0,255);
				case              'q': quiet = 1; noisy = 0; break;
				case     SET_PARM('Q',"queue-depth",dma_q,0,32);
				case     SET_PARM('s',"powerup-in-standby",powerup_in_standby,0,1);
				case     SET_PARM('S',"standby-interval",standby,0,255);
				case GET_SET_PARM('r',"read-only",readonly,0,1);
				case      DO_FLAG('t',do_timings);
				case      DO_FLAG('T',do_ctimings);
				case GET_SET_PARM('u',"unmask-irq",unmask,0,1);
				case     SET_PARM('U',"unregister-interface",unregister,0,63);
				case      DO_FLAG('v',do_defaults);
				case              'V': fprintf(stdout, "%s %s\n", progname, VERSION); exit(0);
				case     SET_FLAG('w',doreset);
				case GET_SET_PARM('W',"write-cache",wcache,0,1);
				case     SET_PARM('x',"tristate-device",tristate,0,1);
				case     SET_FLAG('y',standbynow);
				case     SET_FLAG('Y',sleepnow);
				case     SET_FLAG('z',reread_partn);
				case     SET_FLAG('Z',seagate);

				case '-':
					if (get_longarg())
						disallow_flags = 1;
					break;

				case 'p':
					get_piomode = noisy;
					noisy = 1;
					GET_XFERMODE(set_piomode,piomode);
					break;

				case 'X':
					get_xfermode = noisy;
					noisy = 1;
					GET_XFERMODE(set_xfermode,xfermode_requested);
					if (!set_xfermode)
						fprintf(stderr, "-X: missing value\n");
					break;

				case 'R':
					if (!*argp && argc && isdigit(**argv))
						argp = *argv++, --argc;
					if(! argp) {
						fprintf(stderr, "expected hwif_data\n");
						exit(EINVAL);
					}
					sscanf(argp++, "%i", &hwif_data);
					if (argc && isdigit(**argv))
						argp = *argv++, --argc;
					else {
						fprintf(stderr, "expected hwif_ctrl\n");
						exit(EINVAL);
					}
					sscanf(argp, "%i", &hwif_ctrl);
					if (argc && isdigit(**argv))
						argp = *argv++, --argc;
					else {
						fprintf(stderr, "expected hwif_irq\n");
						exit(EINVAL);
					}
					sscanf(argp, "%i", &hwif_irq);
					*argp = '\0';
					scan_hwif = 1;
					break;

				default:
					usage_help(EINVAL);
			}
			num_flags_processed++;
		}
		if (!argc)
			usage_help(EINVAL);
	}
	exit (0);
}
