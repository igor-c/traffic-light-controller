#!/bin/bash

set -e

PASSWORD="raspberry"
ADDR="192.168.1.55"
DEST="pi@$ADDR:/home/pi/traffic-light-controller/client/"

sshpass -p "$PASSWORD" scp -p *.cc *.h Makefile $DEST
sshpass -p "$PASSWORD" scp -p images/still/* $DEST/images/still/

