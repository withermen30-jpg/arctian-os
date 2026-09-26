#!/bin/bash

set -e

if [ "$1" = "clean" ]; then
    make clean
    exit 0
fi

make -B

if [ "$1" = "qemu" ]; then
    make qemu
fi
