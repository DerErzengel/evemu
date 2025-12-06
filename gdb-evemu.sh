#!/bin/bash
# Debug evemu-play with local library
export LD_LIBRARY_PATH=/home/noah/Projects/evemu/src/.libs:$LD_LIBRARY_PATH
gdb --args ./evemu-play "$@"
