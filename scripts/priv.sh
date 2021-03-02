#!/bin/sh

function main
  {
    echo "Connecting to nsfdsuds server..."
    "$NSFDSUDS" --client "$SOCKETFILE" "$NSENTER" && ok
    echo "Something went wrong"
  }

function ok
  {
    echo "Something went right.  Sleeping forever, one day at a time..."
    while true
      do
        sleep 1d
      done
  }

main
