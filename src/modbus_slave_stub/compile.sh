#!/bin/bash

BASEDIR="$( cd "$(dirname "$0")" ; pwd -P )"
cd "$BASEDIR"

gcc -I ./libmodbus/include/modbus -L ./libmodbus/lib/ -lmodbus  stub.c -o stub
