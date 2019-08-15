#!/bin/bash

# set -ex

# sudo nmap -sS --max-rtt-timeout 100ms --min-parallelism 100 -p 22 192.168.1.0/24 192.168.88.0/24 | grep raspberrypi
# sudo nmap -sS -p 22 192.168.88.0/24 192.168.1.0/24 | grep raspberrypi
# sudo nmap -sS --max-rtt-timeout 100ms -p 22 `ip -o -4 addr list eth0 | awk '{print $4}'` | grep 'scan report for'

fping -a -r1 -g `ip -o -4 addr list eth0 | awk '{print $4}'` &> /dev/null
arp -n | grep -i "b8:27:eb" | cut -d " " -f 1
