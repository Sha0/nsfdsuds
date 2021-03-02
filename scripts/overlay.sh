#!/bin/sh

WORKDIR="$OVERLAYDIR/workdir"
UPPERDIR="$OVERLAYDIR/upperdir"

function main
  {
    echo "Setting up overlay..."
    mkdir -p "$LOWERDIR"
    mkdir -p "$WORKDIR"
    mkdir -p "$UPPERDIR"
    mkdir -p "$NEWROOT"

    # LOWERDIR could be a volume, instead of this example
    mount -o bind / "$LOWERDIR"

    mount -t overlay -o lowerdir="$LOWERDIR",upperdir="$UPPERDIR",workdir="$WORKDIR" overlay "$NEWROOT"

    "$REMOUNTFILE" --doit

    # An example script to put inside
    cp "$INITFILE" "$NEWROOT$INITFILE"

    echo "Noting readiness..."
    touch "$READYFILE"

    echo "Leaving namespace..."
  }

main
