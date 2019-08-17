#!/bin/bash

set -e

PASSWORD="raspberry"
ADDR=$1
# ADDR="192.168.88.54"
# ADDR="192.168.1.55"

if [ -z "$ADDR" ]; then
  echo "Must provide an address"
  exit 1
fi

if [ "$ADDR" = "all" ]; then
  ADDRS=`cat all_pi`
else
  ADDRS=$ADDR
fi

while read x; do
  DEST="pi@$x:/home/pi/controller/"
  sshpass -p "$PASSWORD" scp -p *.cc *.h Makefile $DEST
  # sshpass -p "$PASSWORD" scp -p client $DEST
  # sshpass -p "$PASSWORD" scp -p config.txt $DEST
  # sshpass -p "$PASSWORD" scp -p images/still/* $DEST/images/still/
  # sshpass -p "$PASSWORD" scp -p images/birthday/* $DEST/images/birthday/
done <<< "$ADDRS"
