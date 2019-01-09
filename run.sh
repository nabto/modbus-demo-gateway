#!/bin/bash

DEVICEID=
DEVICEKEY=

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
