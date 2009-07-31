TRIM / wiper script for SATA SSDs   (July 2009)
===============================================

The wiper.sh script is for tuning up SATA SSDs (Solid-State-Drives).

It calculate a list of free (unallocated) blocks within a filesystem,
and informs the SSD firmware of those blocks, so that it can better manage
the underlying media for wear-leveling and garbage-collection purposes.

In some cases, this can restore a sluggish SSD to nearly-new speeds again.

This script may be EXTREMELY HAZARDOUS TO YOUR DATA.

It does work for me here, on a single pre-production SATA SSD.
But it has not yet been throroughly tested by others.

Please back-up your data to a *different* physical drive before trying it.
And if you are at all worried, then DO NOT USE THIS SCRIPT!!

Once there are drives in the marketplace with production firmware that supports
the SATA DSM TRIM command, then this will get tested a bit more over time.
When that happens, it will be moved out of this directory and installed alongside
the hdparm executable, probably under /sbin or /usr/sbin.

Until then, DO NOT USE THIS SCRIPT if you cannot afford losing your data!!


wiper.sh :

	This script works for read-write mounted ext4 filesystems,
	and for read-only mounted/unmounted ext2, ext3, and ext4 filesystems.

	Invoke the script with the pathname to the mounted filesystem
	or the block device path for the filesystem.

		Eg.	./wiper.sh /boot
			./wiper.sh /
			./wiper.sh /dev/sda1

	Note that the most comprehensive results are achieved when
	wiping a filesystem that is not currently mounted read-write,
	though the difference is small.

================================================

The sil24_trim_protocol_fix.patch file in this directory is a kernel
patch for all recent Linux kernel versions up to and including 2.6.31.

This fixes the kernel device driver for the Silicon Image SiI-3132
SATA controller to correctly pass DSM/TRIM commands to the drives.

If you use this hardware in your system, then you will need to apply
the patch to your kernel before using the wiper scripts.

