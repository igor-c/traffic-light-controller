#!/bin/bash

set -e

PASSWORD="raspberry"
ADDR=$1
# ADDR="192.168.88.54"

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
  SSH_HOST="pi@$x"
  DEST="$SSH_HOST:/home/pi/controller/"
  # echo "$PASSWORD $SSH_HOST"
  #set +e
  sshpass -p "$PASSWORD" ssh $SSH_HOST 'sudo reboot' &
  #set -e
done <<< "$ADDRS"
