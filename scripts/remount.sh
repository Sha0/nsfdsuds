#!/bin/sh

function main
  {
    if [ "$1" == "--doit" ]
      then
        DOIT="YES"
      fi
    if [ "$1" == "--undo" ]
      then
        undo_mounts
        exit
      fi
    mkdir -p "$NEWROOT"
    cat /proc/1/mountinfo | awk '{print $5}' | grep -v "^/$" | grep -v "^$EXCLUDEDIR/" | sort | while read MNT G
      do
        each_mount "$MNT"
      done
  }

function dry_run
  {
    if [ "$DOIT" == "YES" ]
      then
        "$@"
      else
        echo "$@"
      fi
  }

function each_mount
  {
    if test -d "$1"
      then
        mount_dir "$1"
      else
        mount_file "$1"
      fi
  }

function mount_dir
  {
    echo Bind-mounting directory "$1"
    dry_run mkdir -p "$NEWROOT$1"
    dry_run mount -o bind "$1" "$NEWROOT$1"
  }

function mount_file
  {
    echo Bind-mounting file "$1"
    dirname "$1" | while read DIRNAME G
      do
        dry_run mkdir -p "$NEWROOT$DIRNAME"
      done
    dry_run touch "$NEWROOT$1"
    dry_run mount -o bind "$1" "$NEWROOT$1"
  }

function undo_mounts
  {
    cat /proc/1/mountinfo | awk '{print $5}' | grep "^$NEWROOT/" | sort -r | while read MNT
      do
        umount "$MNT"
      done
  }

main "$1"
