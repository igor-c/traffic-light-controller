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

REMOTE="pi@$ADDR:/home/pi/traffic-light-controller/client/"
sshpass -p "$PASSWORD" scp -p $REMOTE/client ./
