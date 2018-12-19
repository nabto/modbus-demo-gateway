#!/bin/bash

DEVICEID=3brkqf77.ev9dbf.appmyproduct.com
DEVICEKEY=80e652c9d9610bff9ae75479372a2a4d

if test "x$DEVICEID" == "x" -o "x$DEVICEKEY" == "x"; then
  echo "Please provide DEVICEID and DEVICEKEY from appmyproduct.com"
  exit -1
fi

if test ! -f ./build/modbus_device_stub; then
  echo "Please run ./build.sh .. no compiled binary found"
  exit -1
fi


# Example to run
./build/modbus_device_stub -d $DEVICEID -k $DEVICEKEY
