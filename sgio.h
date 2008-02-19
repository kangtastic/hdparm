/* prototypes and stuff for ATA command ioctls */

#include <linux/types.h>

enum {
	ATA_OP_READ_PIO			= 0x20,
	ATA_OP_READ_PIO_ONCE		= 0x21,
	ATA_OP_READ_LONG		= 0x22,
	ATA_OP_READ_LONG_ONCE		= 0x23,
	ATA_OP_READ_PIO_EXT		= 0x24,
	ATA_OP_READ_DMA_EXT		= 0x25,
	ATA_OP_WRITE_PIO		= 0x30,
	ATA_OP_WRITE_LONG		= 0x32,
	ATA_OP_WRITE_LONG_ONCE		= 0x33,
	ATA_OP_WRITE_PIO_EXT		= 0x34,
	ATA_OP_WRITE_DMA_EXT		= 0x35,
	ATA_OP_READ_VERIFY_ONCE		= 0x41,
	ATA_OP_WRITE_UNC_EXT		= 0x45,	// lba48, no data, uses feat reg
	ATA_OP_STANDBYNOW2		= 0x94,
	ATA_OP_SETIDLE2			= 0x97,
	ATA_OP_CHECKPOWERMODE2		= 0x98,
	ATA_OP_SLEEPNOW2		= 0x99,
	ATA_OP_PIDENTIFY		= 0xa1,
	ATA_OP_READ_NATIVE_MAX		= 0xf8,
	ATA_OP_READ_NATIVE_MAX_EXT	= 0x27,
	ATA_OP_SMART			= 0xb0,
	ATA_OP_READ_DMA			= 0xc8,
	ATA_OP_WRITE_DMA		= 0xca,
	ATA_OP_DOORLOCK			= 0xde,
	ATA_OP_DOORUNLOCK		= 0xdf,
	ATA_OP_STANDBYNOW1		= 0xe0,
	ATA_OP_SETIDLE1			= 0xe3,
	ATA_OP_SET_MAX			= 0xf9,
	ATA_OP_SET_MAX_EXT		= 0x37,
	ATA_OP_CHECKPOWERMODE1		= 0xe5,
	ATA_OP_SLEEPNOW1		= 0xe6,
	ATA_OP_FLUSHCACHE		= 0xe7,
	ATA_OP_FLUSHCACHE_EXT		= 0xea,
	ATA_OP_IDENTIFY			= 0xec,
	ATA_OP_SETFEATURES		= 0xef,
	ATA_OP_SECURITY_SET_PASS	= 0xf1,
	ATA_OP_SECURITY_UNLOCK		= 0xf2,
	ATA_OP_SECURITY_ERASE_PREPARE	= 0xf3,
	ATA_OP_SECURITY_ERASE_UNIT	= 0xf4,
	ATA_OP_SECURITY_FREEZE_LOCK	= 0xf5,
	ATA_OP_SECURITY_DISABLE		= 0xf6,
};

enum {	/* ioctl() numbers */
	HDIO_DRIVE_CMD		= 0x031f,
	HDIO_DRIVE_RESET	= 0x031c,
	HDIO_DRIVE_TASK		= 0x031e,
	HDIO_DRIVE_TASKFILE	= 0x031d,
	HDIO_GETGEO		= 0x0301,
	HDIO_GETGEO_BIG		= 0x0330,
	HDIO_GET_32BIT		= 0x0309,
	HDIO_GET_ACOUSTIC	= 0x030f,
	HDIO_GET_BUSSTATE	= 0x031a,
	HDIO_GET_DMA		= 0x030b,
	HDIO_GET_IDENTITY	= 0x030d,
	HDIO_GET_KEEPSETTINGS	= 0x0308,
	HDIO_GET_MULTCOUNT	= 0x0304,
	HDIO_GET_NOWERR		= 0x030a,
	HDIO_GET_QDMA		= 0x0305,
	HDIO_GET_UNMASKINTR	= 0x0302,
	HDIO_OBSOLETE_IDENTITY	= 0x0307,
	HDIO_SCAN_HWIF		= 0x0328,
	HDIO_SET_32BIT		= 0x0324,
	HDIO_SET_ACOUSTIC	= 0x032c,
	HDIO_SET_BUSSTATE	= 0x032d,
	HDIO_SET_DMA		= 0x0326,
	HDIO_SET_KEEPSETTINGS	= 0x0323,
	HDIO_SET_MULTCOUNT	= 0x0321,
	HDIO_SET_NOWERR		= 0x0325,
	HDIO_SET_PIO_MODE	= 0x0327,
	HDIO_SET_QDMA		= 0x032e,
	HDIO_SET_UNMASKINTR	= 0x0322,
	HDIO_SET_WCACHE		= 0x032b,
	HDIO_TRISTATE_HWIF	= 0x031b,
	HDIO_UNREGISTER_HWIF	= 0x032a,
	CDROM__SPEED		= 0x5322,
};

/*
 * Some useful ATA register bits
 */
enum {
	ATA_USING_LBA		= (1 << 6),
	ATA_STAT_DRQ		= (1 << 3),
	ATA_STAT_ERR		= (1 << 0),
};

/*
 * Useful parameters for init_hdio_taskfile():
 */
enum {	RW_READ			= 0,
	RW_WRITE		= 1,
	LBA28_OK		= 0,
	LBA48_FORCE		= 1,
};

/*
 * Definitions and structures for use with SG_IO + ATA_16:
 */
struct ata_lba_regs {
	__u8	feat;
	__u8	nsect;
	__u8	lbal;
	__u8	lbam;
	__u8	lbah;
};
struct ata_tf {
	__u8			dev;
	__u8			command;
	__u8			error;
	__u8			status;
	__u8			is_lba48;
	struct ata_lba_regs	lob;
	struct ata_lba_regs	hob;
};

/*
 * Definitions and structures for use with HDIO_DRIVE_TASKFILE:
 */

enum {
	/*
	 * These (redundantly) specify the category of the request
	 */
	TASKFILE_CMD_REQ_NODATA	= 0,	/* ide: IDE_DRIVE_TASK_NO_DATA */
	TASKFILE_CMD_REQ_IN	= 2,	/* ide: IDE_DRIVE_TASK_IN */
	TASKFILE_CMD_REQ_OUT	= 3,	/* ide: IDE_DRIVE_TASK_OUT */
	TASKFILE_CMD_REQ_RAW_OUT= 4,	/* ide: IDE_DRIVE_TASK_RAW_WRITE */
	/*
	 * These specify the method of transfer (pio, dma, multi, ..)
	 */
	TASKFILE_DPHASE_NONE	= 0,	/* ide: TASKFILE_IN */
	TASKFILE_DPHASE_PIO_IN	= 1,	/* ide: TASKFILE_IN */
	TASKFILE_DPHASE_PIO_OUT	= 4,	/* ide: TASKFILE_OUT */
};

union reg_flags {
	unsigned all			:16;
	struct {
		unsigned data		: 1;
		unsigned feat		: 1;
		unsigned lbal		: 1;
		unsigned nsect		: 1;
		unsigned lbam		: 1;
		unsigned lbah		: 1;
		unsigned dev		: 1;
		unsigned command	: 1;

		unsigned hob_data	: 1;
		unsigned hob_feat	: 1;
		unsigned hob_lbal	: 1;
		unsigned hob_nsect	: 1;
		unsigned hob_lbam	: 1;
		unsigned hob_lbah	: 1;
		unsigned hob_dev	: 1;
		unsigned hob_command	: 1;
	} b;
};

struct taskfile_regs {
	__u8	data;
	__u8	feat;
	__u8	nsect;
	__u8	lbal;
	__u8	lbam;
	__u8	lbah;
	__u8	dev;
	__u8	command;
};

struct hdio_taskfile {
	struct taskfile_regs	lob;
	struct taskfile_regs	hob;
	union reg_flags		oflags;
	union reg_flags		iflags;
	int			dphase;
	int			cmd_req;     /* IDE command_type */
	unsigned long		obytes;
	unsigned long		ibytes;
	__u16			data[0];
};

void tf_init (struct ata_tf *tf, __u8 ata_op, __u64 lba, unsigned int nsect);
__u64 tf_to_lba (struct ata_tf *tf);
int sg16 (int fd, int rw, struct ata_tf *tf, void *data, unsigned int data_bytes, unsigned int timeout_secs);
int do_drive_cmd (int fd, unsigned char *args);
int do_taskfile_cmd (int fd, struct hdio_taskfile *r, unsigned int timeout_secs);
int dev_has_sgio (int fd);
void init_hdio_taskfile (struct hdio_taskfile *r, __u8 ata_op, int rw, int force_lba48,
				__u64 lba, unsigned int nsect, int data_bytes);
