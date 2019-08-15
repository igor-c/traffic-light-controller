#!/bin/bash

set -e

sudo nmap -sS --max-rtt-timeout 100ms -p 22 192.168.1.0/24 192.168.88.0/24 | grep raspberrypi
