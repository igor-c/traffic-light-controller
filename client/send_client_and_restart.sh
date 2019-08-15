#!/bin/bash

set -ex

PASSWORD="raspberry"
ADDR=$1
# ADDR="192.168.88.54"
# ADDR="192.168.1.55"

if [ -z "$ADDR" ]; then
  echo "Must provide an address"
  exit 1
fi

if [ $ADDR="all" ]; then
  ADDRS=`./find_all.sh`
else
  ADDRS=$ADDR
fi

for x in "$ADDRS"; do
  DEST="pi@$x:/home/pi/traffic-light-controller/client/"
  sshpass -p "$PASSWORD" ssh $x 'echo sudo killall client'
  sshpass -p "$PASSWORD" scp -p client $DEST
  sshpass -p "$PASSWORD" ssh $x 'sudo reboot'
done
