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

if [ $ADDR="all" ]; then
  ADDRS=`cat all_pi`
else
  ADDRS=$ADDR
fi

for x in "$ADDRS"; do
  SSH_HOST="pi@$x"
  DEST="$SSH_HOST:/home/pi/traffic-light-controller/client/"
  sshpass -p "$PASSWORD" ssh $SSH_HOST 'sudo killall client'
  sshpass -p "$PASSWORD" scp -p client $DEST
  sshpass -p "$PASSWORD" ssh $SSH_HOST 'sudo reboot'
done
