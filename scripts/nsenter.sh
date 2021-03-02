#!/bin/sh

function main
  {
    echo "Testing FDs for mount namespace..."
    cd /proc/self/fd/
    for FD in *
      do
        test_mnt_ns_fd "$FD"
      done
    echo "Did not find mount namespace FD"
    exit 1
  }

function mnt_ns_fd
  {
    echo "Mount namespace FD found.  Entering namespace..."
    nsenter -m"$1" "$SETUPFILE"
    exit $?
  }

function test_mnt_ns_fd
  {
    readlink "$1" | grep "^mnt:" > /dev/null && mnt_ns_fd "$1"
  }

main
