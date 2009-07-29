TRIM / wiper scripts for SATA SSDs   (July 2009)
================================================

This pair of scripts are for tuning up SATA SSDs (Solid-State-Drives).

These calculate a list of free (unallocated) blocks within a filesystem,
and inform the SSD firmware of those blocks, so that it can better manage
the underlying media for wear-leveling and garbage-collection purposes.

In some cases, this can restore a sluggish SSD to nearly-new speeds again.

These scripts may be EXTREMELY HAZARDOUS TO YOUR DATA.

They do work for me here, on a single pre-production SATA SSD.
But neither of these scripts has been throroughly tested by others yet.
Please back-up your data to a *different* physical drive before trying them.
And if you are at all worried, then DO NOT USE THESE SCRIPTS!!

Once we see drives in the marketplace with production firmware that supports
the SATA DSM TRIM command, then these will get tested a bit more over time.
When that happens, they'll be moved out of this directory and installed alongside
the hdparm executable, probably under /sbin or /usr/sbin.

Until then, DO NOT USE THESE SCRIPTS if you cannot afford losing your data!!


wiper.sh.online :

	This script works for mounted (read-write) ext4 filesystems only.
	Invoke the script with the pathname to the mounted filesystem.

		Eg.	./wiper.sh.online /boot
		or	./wiper.sh.online /


wiper.sh.offline :

	This version is for unmounted filesystems, or read-only filesystems.
	It should work (only) for ext2, ext3, and ext4 filesystem types,
	though it has only been tested with ext3 and ext4.
	Invoke the script with the block device path of the filesystem.

		Eg.	./wiper.sh.offline /dev/sda1

