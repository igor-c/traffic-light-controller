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
  # sshpass -p "$PASSWORD" ssh $SSH_HOST 'sudo sync' &
  sshpass -p "$PASSWORD" ssh $SSH_HOST 'sudo reboot' &
  # sshpass -p "$PASSWORD" ssh $SSH_HOST 'date' &
  # sshpass -p "$PASSWORD" ssh $SSH_HOST 'sudo hwclock -r' &
  # sshpass -p "$PASSWORD" ssh $SSH_HOST sudo ls -l /boot/config.txt &
  # sshpass -p "raspberry" ssh pi@192.168.88.54 sudo cp /home/pi/boot_config.txt /boot/config.txt
  #set -e
done <<< "$ADDRS"
