#!/bin/bash

BASEDIR="$( cd "$(dirname "$0")" ; pwd -P )"
cd "$BASEDIR"

mkdir build
cd build
cmake ..
make

