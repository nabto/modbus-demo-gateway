#!/bin/bash

BASEDIR="$( cd "$(dirname "$0")" ; pwd -P )"
cd "$BASEDIR"

# Example to run the stub
# Replace ptyp0 to point to the right pseudo terminal on your system
./stub /dev/ptyp0
