#!/bin/bash



BASEDIR="$( cd "$(dirname "$0")" ; pwd -P )"
cd "$BASEDIR"

tar -zxf ./libmodbus-3.1.4.tar.gz

cd libmodbus-3.1.4
./configure --prefix=$BASEDIR/libmodbus/  --disable-shared --enable-static
make install 

echo
echo
echo "This script should now have installed a static library version of libmodbus-3.1.4 in the directory libmodbus"
echo "Remember libmodbus is under LGPL, ie. static libray requires source code"
