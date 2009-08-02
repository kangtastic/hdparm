#!/bin/bash
#
# SATA SSD free-space TRIM utility, version 1.5 by Mark Lord
#
# Copyright (C) 2009 Mark Lord.  All rights reserved.
#
# Requires gawk, a really-recent hdparm, and various other programs.
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License Version 2,
# as published by the Free Software Foundation.
# 
# This program is distributed in the hope that it would be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

## Things we (may) need on various paths through this script:
##
XFS_REPAIR=/sbin/xfs_repair
XFS_DB=/usr/sbin/xfs_db
DUMPE2FS=/sbin/dumpe2fs
HDPARM=/sbin/hdparm
GAWK=/usr/bin/gawk
BLKID=/sbin/blkid
GREP=/bin/grep
ID=/usr/bin/id
LS=/bin/ls
DF=/bin/df
RM=/bin/rm

## Check for needed programs and give a nicer error message than we'd otherwise see:
##
function require_prog(){
	while [ "$1" != "" ]; do
		if [ ! -x "$1" ]; then
			echo "$1: needed but not found, aborting." >&2
			exit 1
		fi
		shift
	done
}

echo
require_prog $HDPARM $GAWK $BLKID $GREP $ID $LS $DF $RM

## I suppose this will confuse the three SELinux users out there:
##
if [ `$ID -u` -ne 0 ]; then
	echo "Only the super-user can use this (try \"sudo $0\" instead), aborting." >&2
	exit 1
fi

## We need a very modern hdparm, for its --fallocate and --trim-sector-ranges flags:
## Version 9.22 added an ext4/FIEMAP workaround and fsync() inside --fallocate
##
HDPVER=`$HDPARM -V | $GAWK '{gsub("[^0-9.]","",$2); if ($2 > 0) print ($2 * 100); else print 0; exit(0)}'`
if [ $HDPVER -lt 922 ]; then
	echo "$HDPARM: version >= 9.22 is required, aborting." >&2
	exit 1
fi

## The usual terse usage information:
##
function usage_error(){
	echo >&2
	echo "Linux tune-up (TRIM) utility for SATA SSDs"
	echo "Usage:  $0 [--verbose] [--commit] <mount_point|block_device>" >&2
	echo "   Eg:  $0 /dev/sda1" >&2
	echo >&2
	exit 1
}

## Parameter parsing for the main script.
## Yeah, we could use getopt here instead, but what fun would that be?
##
verbose=0
commit=""
target=""
method=""
argc=$#
while [ $argc -gt 0 ]; do
	if [ "$1" = "--commit" ]; then
		commit=yes
	elif [ "$1" = "--verbose" ]; then
		verbose=1
	elif [ "$1" = "" ]; then
		usage_error
	else
		[ "$target" != "" ] && usage_error
		if [ "$1" != "${1##* }" ]; then
			echo "\"$1\": pathname has embedded blanks, aborting." >&2
			exit 1
		fi
		target="$1"
		[ "$target" != "/" ] && target="${target%*/}"
		[ "${target:0:1}" = "/" ] || usage_error
		[ -d "$target" -a ! -h "$target" ] && method=online
		[ -b "$target" ] && method=offline
		[ "$method" = "" ] && usage_error
	fi
	argc=$((argc - 1))
	shift
done
[ "$target" = "" ] && usage_error

## Find the active mount-point (fsdir) associated with a device ($1: fsdev).
## This is complicated, and probably still buggy, because a single
## device can show up under *multiple* mount points in /proc/mounts.
##
function get_fsdir(){
	$GAWK -v p="$1" '{
		if ($1 == p) {
			if (rw != "rw") {
				rw=substr($4,1,2)
				r = $2
			}
		}
	} END{print r}' < /proc/mounts
}

## Find the device (fsdev) associated with a mount point ($1: fsdir).
## Since mounts can be stacked on top of each other, we return the
## one from the last occurance in the list from /proc/mounts.
##
function get_fsdev(){   ## from fsdir
	$GAWK -v p="$1" '{if ($2 == p) r=$1} END{print r}' < /proc/mounts
}

## Find the r/w or r/o status (fsmode) of a filesystem mount point  ($1: fsdir)
## We get it from the last occurance of the mount point in the list from /proc/mounts,
## and convert it to a longer human-readable string.
##
function get_fsmode(){  ## from fsdir
	mode="`$GAWK -v p="$1" '{if ($2 == p) r=substr($4,1,2)} END{print r}' < /proc/mounts`"
	if [ "$mode" = "ro" ]; then
		echo "read-only"
	elif [ "$mode" = "rw" ]; then
		echo "read-write"
	else
		echo "$fsdir: unable to determine mount status, aborting." >&2
		exit 1
	fi
}

## Use $DF to determine the device name associated with the root filesystem.
##
rootdev="`($DF -P / | $GAWK '/^[/]/{print $1;exit}') 2>/dev/null`"

## The user gave us a directory (mount point) to TRIM,
## which implies that we will be doing an online TRIM
## using --fallocate and --fibmap to find the free extents.
## Do some preliminary correctness/feasibility checks on fsdir:
##
if [ "$method" = "online" ]; then
	## Ensure fsdir exists and is accessible to us:
	fsdir="$target"
	cd "$fsdir" || exit 1

	## Figure out what device holds the filesystem.
	fsdev="`get_fsdev $fsdir`"
	if [ "$fsdev" = "" ]; then
		echo "$fsdir: not found in /proc/mounts, aborting." >&2
		exit 1
	fi

	## The root filesystem may show up as the phoney "/dev/root" device
	## in /proc/mounts (ugh).  So if we see that, then substitute the rootdev
	## that $DF gave us earlier.
	##
	[ ! -e "$fsdev" -a "$fsdev" = "/dev/root" ] && fsdev="$rootdev"

	## Ensure that fsdev exists and is a block device:
	if [ ! -e "$fsdev" ]; then
		echo "$fsdev: not found" >&2
		exit 1
	fi
	if [ ! -b "$fsdev" ]; then
		echo "$fsdev: not a block device" >&2
		exit 1
	fi

	## If it is mounted read-only, we must switch to doing an "offline" trim of fsdev:
	fsmode="`get_fsmode $fsdir`"
	[ "$fsmode" = "read-only" ] && method=offline
fi

## This is not an "else" clause from the above, because "method" may have changed.
## For offline TRIM, we need the block device, and it cannot be mounted read-write:
##
if [ "$method" = "offline" ]; then
	## We might already have fsdev/fsdir from above; if not, we need to find them.
	if [ "$fsdev" = "" -o "$fsdir" = "" ]; then
		fsdev="$target"
		fsdir="`get_fsdir $fsdev`"
		## More weirdness for /dev/root in /proc/mounts:
		[ "$fsdir" = "" -a "$fsdev" = "$rootdev" ] && fsdir="`get_fsdir /dev/root`"
	fi

	## If the filesystem is truly not-mounted, then fsdir will still be empty here.
	## It could be mounted, though.  Read-only is fine, but read-write means we need
	## to switch gears and do an "online" TRIM instead of an "offline" TRIM.
	##
	if [ "$fsdir" != "" ]; then
		fsmode="`get_fsmode $fsdir`"
		if [ "$fsmode" = "read-write" ]; then
			method=online
			cd "$fsdir" || exit 1
		fi
	fi
fi

## Use $LS to find the major number of a block device:
##
function get_major(){
	$LS -ln "$1" | $GAWK '{print gensub(",","",1,$5)}'
}

## At this point, we have finalized our selection of online vs. offline,
## and we definitely know the fsdev, as well as the fsdir (fsdir="" if not-mounted).
##
## Now guess at the underlying rawdev name, which could be exactly the same as fsdev.
## Then determine whether or not rawdev claims support for TRIM commands.
## Note that some devices lie about support, and later reject the TRIM commands.
##
rawdev=`echo $fsdev | $GAWK '{print gensub("[0-9]*$","","g")}'`
if [ ! -e "$rawdev" ]; then
	rawdev=""
elif [ ! -b "$rawdev" ]; then
	rawdev=""
elif [ "`get_major $fsdev`" -ne "`get_major $rawdev`" ]; then  ## sanity check
	rawdev=""
elif ! $HDPARM -I $rawdev | $GREP -i '[ 	][*][ 	]*Data Set Management TRIM supported' &>/dev/null ; then
	if [ "$commit" = "yes" ]; then
		echo "$rawdev: DSM/TRIM command not supported, aborting." >&2
		exit 1
	fi
	echo "$rawdev: DSM/TRIM command not supported (continuing with dry-run)." >&2
fi
if [ "$rawdev" = "" ]; then
	echo "$fsdev: unable to reliably determine the underlying physical device name, aborting" >&2
	exit 1
fi

## We also need to know the offset of fsdev from the beginning of rawdev,
## because TRIM requires absolute sector numbers within rawdev:
##
fsoffset=`$HDPARM -g "$fsdev" | $GAWK 'END {print $NF}'`

## Next step is to determine what type of filesystem we are dealing with (fstype):
##
if [ "$fsdir" = "" ]; then
	## Not mounted: use $BLKID to determine the fstype of fsdev:
	fstype=`$BLKID -w /dev/null -c /dev/null $fsdev 2>/dev/null | \
		 $GAWK '/ TYPE=".*"/{sub("^.* TYPE=\"",""); sub("[\" ][\" ]*.*$",""); print}'`
	[ $verbose -gt 0 ] && echo "$fsdev: fstype=$fstype"
else
	## Mounted: we could just use $BLKID here, too, but it's safer to use /proc/mounts directly:
	fstype="`$GAWK -v p="$fsdir" '{if ($2 == p) r=$3} END{print r}' < /proc/mounts`"
	[ $verbose -gt 0 ] && echo "$fsdir: fstype=$fstype"
fi
if [ "$fstype" = "" ]; then
	echo "$fsdev: unable to determine filesystem type, aborting." >&2
	exit 1
fi

## Some helper funcs and vars for use with the xfs filesystem tools:
##
function xfs_abort(){
	echo "$fsdev: unable to determine xfs filesystem ${1-parameters}, aborting." >&2
	exit 1
}
function xfs_trimlist(){
	$XFS_DB -r -c "freesp -d" "$fsdev"  ## couldn't get this to work inline
}
xfs_agoffsets=""
xfs_blksects=0

## Now figure out whether we can actually do TRIM on this type of filesystem:
##
if [ "$method" = "online" ]; then

	if [ "$fstype" = "ext2" -o "$fstype" = "ext3" ]; then  ## No --fallocate support
		echo "$target: cannot TRIM $fstype filesystem when mounted read-write, aborting." >&2
		exit 1
	fi

	## Figure out if we have enough free space to even attempt TRIM:
	##
	freesize=`$DF -P -B 1024 . | $GAWK -v p="$fsdev" '{if ($1 == p) r=$4} END{print r}'`
	if [ $freesize -lt 15000 ]; then
		echo "$target: filesystem too full for TRIM, aborting." >&2
		exit 1
	fi

	## Figure out how much space to --fallocate (later), keeping in mind
	## that this is a live filesystem, and we need to leave some space for
	## other concurrent activities, as well as for filesystem overhead (metadata).
	## So, reserve at least 1% or 7500 KB, whichever is larger:
	##
	reserved=$((freesize / 100))
	[ $reserved -lt 7500 ] && reserved=7500
	[ $verbose -gt 0 ] && echo "freesize = ${freesize} KB, reserved = ${reserved} KB"
	tmpsize=$((freesize - reserved))
	tmpfile="WIPER_TMPFILE.$$"
	get_trimlist="$HDPARM --fibmap $tmpfile"
else
	## We can only do offline TRIM on filesystems that we "know" about here.
	## Currently, this includes the ext2/3/4 family, and xfs.
	## The first step for any of these is to ensure that the filesystem is "clean",
	## and immediately abort if it is not.
	##
	get_trimlist=""
	if [ "$fstype" = "ext2" -o "$fstype" = "ext3" -o "$fstype" = "ext4" ]; then
		require_prog $DUMPE2FS
		fstate="`$DUMPE2FS $fsdev 2>/dev/null | $GAWK '/^[Ff]ilesystem state:/{print $NF}' 2>/dev/null`"
		if [ "$fstate" != "clean" ]; then
			echo "$target: filesystem not clean, please run \"e2fsck $fsdev\" first, aborting." >&2
			exit 1
		fi
		get_trimlist="$DUMPE2FS $fsdev"
	elif [ "$fstype" = "xfs" ]; then
		require_prog $XFS_REPAIR $XFS_DB
		if ! $XFS_REPAIR -n "$fsdev" &>/dev/null ; then
			echo "$fsdev: filesystem not clean, please run \"xfs_repair $fsdev\" first, aborting." >&2
			exit 1
		fi

		## For xfs, life is more complex than with ext2/3/4 above.
		## The $XFS_DB tool does not return absolute block numbers for freespace,
		## but rather gives them as relative to it's allocation groups (ag's).
		## So, we'll need to interogate it for the offset of each ag within the filesystem.
		## The agoffsets are extracted from $XFS_DB as sector offsets within the fsdev.
		##
		agcount=`$XFS_DB -r -c "sb" -c "print agcount" "$fsdev" | $GAWK '{print 0 + $NF}'`
		[ "$agcount" = "" -o "$agcount" = "0" ] && xfs_abort "agcount"
		xfs_agoffsets=
		i=0
		while [ $i -lt $agcount ]; do
			agoffset=`$XFS_DB -r -c "sb" -c "convert agno $i daddr" "$fsdev" \
				| $GAWK '{print 0 + gensub("[( )]","","g",$2)}'`
			[ "$agoffset" = "" ] && xfs_abort "agoffset-$i"
			[ $i -gt 0 ] && [ $agoffset -le ${xfs_agoffsets##* } ] && xfs_abort "agoffset[$i]"
			xfs_agoffsets="$xfs_agoffsets $agoffset"
			i=$((i + 1))
		done
		xfs_agoffsets="${xfs_agoffsets:1}"	## strip leading space

		## We also need xfs_blksects for later, because freespace gets listed as block numbers.
		##
		blksize=`$XFS_DB -r -c "sb" -c "print blocksize" "$fsdev" | $GAWK '{print 0 + $NF}'`
		[ "$blksize" = "" -o "$blksize" = "0" ] && xfs_abort "block size"
		xfs_blksects=$((blksize/512))
		get_trimlist="xfs_trimlist"
	fi
	if [ "$get_trimlist" = "" ]; then
		echo "$target: offline TRIM not supported for $fstype filesystems, aborting." >&2
		exit 1
	fi
fi

## All ready.  Now let the user know exactly what we intend to do:
##
mountstatus="$fstype non-mounted"
[ "$fsdir" = "" ] || mountstatus="$fstype mounted $fsmode at $fsdir"
echo "Preparing for $method TRIM of free space on $fsdev ($mountstatus)."

## If they specified "--commit" on the command line, then prompt for confirmation first:
##
if [ "$commit" = "yes" ]; then
	echo -n "This operation could destroy your data.  Are you sure (y/N)? " >/dev/tty
	read yn < /dev/tty
	if [ "$yn" != "y" -a "$yn" != "Y" ]; then
		echo "Aborting." >&2
		exit 1
	fi
	dryrun=""
else
	echo "This will be a DRY-RUN only.  Use --commit to do it for real."
	fakeit="# "
	dryrun="(DRY-RUN) "
fi

## Useful in a few places later on:
##
function sync_disks(){
	echo -n "Syncing disks.. "
	sync
	echo
}

## Clean up tmpfile (if any) and exit:
##
function do_cleanup(){
	if [ "$method" = "online" ]; then
		if [ -e $tmpfile ]; then
			echo "Removing temporary file.."
			$RM -f $tmpfile
		fi
		sync_disks
	fi
	[ $1 -eq 0 ] && echo "Done."
	[ $1 -eq 0 ] || echo "Aborted." >&2
	exit $1
}

## Prepare signal handling, in case we get interrupted while $tmpfile exists:
##
function do_abort(){
	echo
	do_cleanup 1
}
trap do_abort SIGTERM
trap do_abort SIGQUIT
trap do_abort SIGINT
trap do_abort SIGHUP

## For online TRIM, go ahead and create the huge temporary file.
## This is where we finally discover whether the filesystem actually
## supports --fallocate or not.  Some folks will be disappointed here.
##
## Note that --fallocate does not actually write any file data to fsdev,
## but rather simply allocates formerly-free space to the tmpfile.
##
if [ "$method" = "online" ]; then
	if [ -e "$tmpfile" ]; then
		if ! $RM -f "$tmpfile" ; then
			echo "$tmpfile: already exists and could not be removed, aborting." >&2
			exit 1
		fi
	fi
	echo -n "Creating temporary file (${tmpsize} KB).. "
	if ! $HDPARM --fallocate "${tmpsize}" $tmpfile ; then
		echo "$target: this kernel may not support 'fallocate' on a $fstype filesystem, aborting." >&2
		exit 1
	fi
	echo
fi

## Finally, we are now ready to TRIM something!
##
## Feed the "get_trimlist" output into a gawk program which will
## extract the trimable lba-ranges (extents) and batch them together
## into huge --trim-sector-ranges calls.
##
## We are limited by two things when doing this:
##   1. Some device drivers may not support more than 255 sectors
##      full of lba:count range data, and
##   2. The hdparm command lines are limited to under 64KB on many systems.
##
sync_disks
echo "Beginning TRIM operations.."
[ $verbose -gt 0 ] && echo "get_trimlist=$get_trimlist"

$get_trimlist 2>/dev/null | $GAWK		\
	-v method="$method"			\
	-v rawdev="$rawdev"			\
	-v dryrun="$dryrun"			\
	-v fsoffset="$fsoffset"			\
	-v verbose="$verbose"			\
	-v xfs_blksects="$xfs_blksects"		\
	-v xfs_agoffsets="$xfs_agoffsets"	\
	-v trim="$fakeit $HDPARM --please-destroy-my-drive --trim-sector-ranges " '

## Begin gawk program
	function do_trim (  mbytes){
		mbytes = "(" int((nsectors+1024)/2048) " MB)"
		print dryrun "Trimming " nranges " free extents encompassing " nsectors " sectors " mbytes
		if (verbose)
			print dryrun trim ranges rawdev
		err = system(trim ranges rawdev " >/dev/null")
		if (err) {
			printf "TRIM command failed, err=%d\n",err > "/dev/stderr"
			exit err
		}
	}
	function append_range (lba,count  ,this_count){
		#printf "append_range(%u, %u)\n", lba, count
		while (count > 0) {
			this_count  = (count > 65535) ? 65535 : count
			this_range  = lba ":" this_count " "
			len        += length(this_range)
			ranges      = ranges this_range
			nsectors   += this_count
			lba        += this_count
			count      -= this_count
			if (len > 64000 || ++nranges >= (255 * 512 / 8)) {
				do_trim()
				ranges   = ""
				len      = 0
				nranges  = 0
				nsectors = 0
			}
		}
	}
	BEGIN {
		if (xfs_agoffsets != "") {	## xfs ?
			method = "xfs_offline"
			agcount = split(xfs_agoffsets,agoffset," ");
		}
	}
	(method == "online") {	## Output from "hdparm --fibmap", in absolute sectors:
		if (NF == 4 && $2 ~ "^[0-9][0-9]*$")
			append_range($2,$4)
		next
	}
	(method == "xfs_offline") { ## Output from xfs_db:
		#print "NF="NF", $1="$1", agcount="agcount", gensub=\""gensub("[0-9 ]","","g",$0)"\""
		if (NF == 3 && gensub("[0-9 ]","","g",$0) == "" && $1 < agcount) {
			lba   = agoffset[1 + $1] + ($2 * xfs_blksects) + fsoffset
			count = $3 * xfs_blksects
			append_range(lba,count)
		}
		next
	}
	/^Block size: *[0-9]/ {	## First stage output from dumpe2fs:
		blksects = $NF / 512
		next
	}
	/^ *Free blocks: [0-9]/	{ ## Bulk of output from dumpe2fs:
		if (blksects) {
			n = split(substr($0,16),f,",*  *")
			for (i = 1; i <= n; ++i) {
				if (f[i] ~ "^[0-9][0-9]*-[0-9][0-9]*$") {
					split(f[i],b,"-")
					lba   = (b[1] * blksects) + fsoffset
					count = (b[2] - b[1] + 1) * blksects
					append_range(lba,count)
				}
			}
		}
	}
	END {
		if (err == 0 && nranges > 0)
			do_trim()
		exit err
	}'
## End gawk program

do_cleanup $?
