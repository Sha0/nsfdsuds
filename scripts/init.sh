#!/bin/sh

function main
  {
    echo "PID $$ started.  Sleeping forever, one day at a time..."
    while true
      do
        sleep 1d
      done
  }

main
