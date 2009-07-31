#!/bin/bash
#
# SATA SSD free-space TRIM utility, version 1.2 by Mark Lord
#
# Copyright (C) 2009 Mark Lord.  All rights reserved.
#
# Requires gawk, dumpe2fs, and hdparm >= 9.17.
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

DUMPE2FS=/sbin/dumpe2fs
GAWK=/usr/bin/gawk
HDPARM=/sbin/hdparm
GREP=/bin/grep
ID=/usr/bin/id
DF=/bin/df
RM=/bin/rm

for prog in $DUMPE2FS $GAWK $HDPARM $GREP $ID $DF $RM ; do
	if [ ! -x $prog ]; then
		echo "$prog: needed but not found, aborting." >&2
		exit 1
	fi
done

if [ `$ID -u` -ne 0 ]; then
	echo "Only the super-user can use this (try \"sudo $0\" instead), aborting." >&2
	exit 1
fi

HDPVER=`$HDPARM -V | $GAWK '{gsub("[^0-9.]","",$2); if ($2 > 0) print ($2 * 100); else print 0; exit(0)}'`
if [ $HDPVER -lt 920 ]; then
	echo "$HDPARM: version >= 9.20 is required, aborting." >&2
	exit 1
fi

function usage_error() {
	echo >&2
	echo "SATA SSD TRIM/wiper utility for Linux ext2/ext3/ext4 filesystems" >&2
	echo "Usage:  $0 [ mount_point | block_device ]" >&2
	echo >&2
	exit 1
}

argc=$#
target=""
method=""
while [ $argc -gt 0 ]; do
	if [ "$1" = "--commit" ]; then
		commit=yes
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

function get_fsdir(){   ## from fsdev; return last rw entry, or last ro entry if no rw
	$GAWK -v p="$1" '{
		if ($1 == p) {
			if (rw != "rw") {
				rw=substr($4,1,2)
				r = $2
			}
		}
	} END{print r}' < /proc/mounts
}

function get_fsdev(){   ## from fsdir
	$GAWK -v p="$1" '{if ($2 == p) r=$1} END{print r}' < /proc/mounts
}

function get_fstype(){  ## from fsdir
	$GAWK -v p="$1" '{if ($2 == p) r=$3} END{print r}' < /proc/mounts
}

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

rootdev="`($DF -P / | $GAWK '/^[/]/{print $1;exit}') 2>/dev/null`"

if [ "$method" = "online" ]; then
	fsdir="$target"
	cd "$fsdir" || exit $?

	fsdev="`get_fsdev $fsdir`"
	if [ "$fsdev" = "" ]; then
		echo "$fsdir: not found in /proc/mounts, aborting." >&2
		exit 1
	fi
	[ ! -e "$fsdev" -a "$fsdev" = "/dev/root" ] && fsdev="$rootdev"
	if [ ! -e "$fsdev" ]; then
		echo "$fsdev: not found" >&2
		exit 1
	fi
	if [ ! -b "$fsdev" ]; then
		echo "$fsdev: not a block device" >&2
		exit 1
	fi

	fsmode="`get_fsmode $fsdir`"
	[ "$fsmode" = "read-only" ] && method=offline
fi

if [ "$method" = "offline" ]; then
	[ "$fsdev" = "" ] && fsdev="$target"
	fsdir="`get_fsdir $fsdev`"
	[ "$fsdir" = "" -a "$fsdev" = "$rootdev" ] && fsdir="`get_fsdir /dev/root`"
	if [ "$fsdir" != "" ]; then
		fsmode="`get_fsmode $fsdir`"
		if [ "$fsmode" = "read-write" ]; then
			method=online
			cd "$fsdir" || exit $?
		fi
	fi
fi

fsoffset=`$HDPARM -g "$fsdev" | $GAWK 'END {print \$NF}'`
rawdev=`echo $fsdev | $GAWK '{print gensub("[0-9]*$","","g")}'`
if [ ! -e "$rawdev" ]; then
	echo "$rawdev: not found" >&2
	exit 1
elif [ ! -b "$rawdev" ]; then
	echo "$rawdev: not a block device" >&2
	exit 1
elif ! $HDPARM -I $rawdev | $GREP -i '[ 	][*][ 	]*Data Set Management TRIM supported' &>/dev/null ; then
	echo "$rawdev: DSM TRIM command not supported" >&2
	exit 1
fi

if [ "$method" = "online" ]; then
	fstype="`get_fstype $fsdir`"
	if [ "$fstype" = "" ]; then
		echo "$target: unable to determine filesystem type, aborting." >&2
		exit 1
	elif [ "$fstype" != "ext4" ]; then
		if [ "$fstype" = "ext2" -o "$fstype" = "ext3" ]; then
			echo "$target: cannot handle $fstype filesystem when mounted read-write, aborting." >&2
		else
			echo "$target: unsupported $fstype filesystem, aborting." >&2
		fi
		exit 1
	fi

	tmpsize=`$DF -P -B 1024 . | $GAWK -v p="$fsdev" '{if ($1 == p) r=$4} END{print r}'`
	if [ $tmpsize -lt 10000 ]; then
		echo "$target: filesystem too full for TRIM, aborting." >&2
		exit 1
	fi
	tmpsize=$((tmpsize - 3000))
	tmpfile="wiper_tmpfile.$$"
	get_trimlist="$HDPARM --fibmap $tmpfile"
else
	if ! $DUMPE2FS "$fsdev" &>/dev/null ; then
		echo "$target: $DUMPE2FS failed, this works only for ext2/ext3/ext4 filesystems, aborting." >&2
		exit 1
	fi
	get_trimlist="$DUMPE2FS $fsdev"
fi

mountstatus="not mounted"
[ "$fsdir" = "" ] || mountstatus="mounted $fsmode at $fsdir"
echo
echo "Preparing for $method TRIM of free space on $fsdev ($mountstatus)."
if [ "$commit" = "yes" ]; then
	echo -n "This operation could destroy your data.  Are you sure (y/N)? " >/dev/tty
	read yn < /dev/tty
	if [ "$yn" != "y" -a "$yn" != "Y" ]; then
		echo "Aborting." >&2
		exit 1
	fi
	fakeit=""
else
	echo "This will be a DRY-RUN only.  Use --commit to do it for real."
	fakeit="# "
fi

function sync_disks(){
	echo -n "Syncing disks.. "
	sync
	echo
}

function do_cleanup(){
	if [ "$method" = "online" ]; then
		if [ -e $tmpfile ]; then
			echo "Removing temporary file.."
			$RM -f $tmpfile
		fi
		sync_disks
	fi
	[ $1 -eq 0 ] && echo "Done."
	[ $1 -eq 0 ] || echo "Aborted."
	exit $1
}

function do_abort(){
	echo
	do_cleanup 1
}

if [ "$method" = "online" ]; then
	trap do_abort SIGINT
	trap do_abort SIGTERM
	trap do_abort SIGQUIT
	trap do_abort SIGHUP
	echo -n "Creating temporary file ($tmpsize KBytes).. "
	$HDPARM --fallocate "${tmpsize}" $tmpfile || exit $?
	echo
fi

sync_disks
echo "Beginning trim operation.."
$get_trimlist 2>/dev/null |	\
$GAWK	-v method="$method"	\
	-v rawdev="$rawdev"	\
	-v fsoffset="$fsoffset"	\
	-v trim="$fakeit $HDPARM --please-destroy-my-drive --trim-sector-ranges " '

## Begin gawk program
	function do_trim () {
		print "Trimming " nranges " free extents encompassing " nsectors " sectors."
		err = system(trim ranges rawdev " >/dev/null")
		if (err) {
			printf "TRIM command failed, err=%d\n", err
			exit err
		}
	}
	function append_range (lba,count  ,this_count) {
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
	(method == "online"){
		if (NF == 4 && $2 ~ "^[0-9][0-9]*$")
			append_range($2,$4)
		next
	}
	/^Group 0:/{
		++ok
	}
	/^Block size: *[0-9]/{
		blksects = $NF / 512
		++ok
	}
	/^  Free blocks: [0-9]/{
		if (ok == 2) {
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
