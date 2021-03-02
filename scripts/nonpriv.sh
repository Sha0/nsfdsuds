#!/bin/sh

function main
  {
    echo "Waiting for nsfdsuds client..."
    "$NSFDSUDS" --server "$SOCKETFILE" && ok
    echo "Something went wrong"
  }

function ok
  {
    echo "Sent namespace FDs.  Waiting for new root..."
    while true
      do
        test -e "$READYFILE" && exec chroot "$NEWROOT" "$INITFILE"
        sleep 5
      done
  }

main
