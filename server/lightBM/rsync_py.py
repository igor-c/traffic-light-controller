import os
import subprocess
from time import sleep
import time
from sys import stdin
# IpList = ['130','123','125','124','122','121']
IpList = ['104']

try:
    from msvcrt import getch
except ImportError:
    def getch():
        import sys, tty, termios
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(sys.stdin.fileno())
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        return ch

print "This script will sync local USBFlash folder with all remote RPi usb dev"
print "Press Y or N to continue"
while True:
    char = getch()
    #char = "Y"
    if char.upper() in ("Y", "N"):
        print char
        break

if char=="Y":
    print 'Enter two last num of your RPi ip (0..255), or write "0" for ALL'
    while True:
        type_sync = int(stdin.readline()) 
        #type_sync = 12
        if type_sync>1 and type_sync<250:
            print "BEGIN.."
            print '192.168.0.'+str(type_sync)
            p=os.popen('rsync -avrz --del -e "sshpass -p raspberry ssh -p 22 -l pi" /Users/Dmitry/GoogleDrive/Freelance/Quest/BurningMen/Qt/media/gif/ pi@192.168.0.'+str(type_sync)+':/home/pi/Script/').read()
            print p
            print "END."
            break
        elif type_sync==0:
            print "BEGIN.."
            #for x in range(11,25):
            for x in IpList:
                print ('192.168.0.'+str(x))
                p=os.popen('rsync -avrz --del -e "sshpass -p raspberry ssh -p 22 -l pi" /Users/Dmitry/GoogleDrive/Freelance/Quest/BurningMen/Qt/media/gif/ pi@192.168.0.'+str(x)+':/home/pi/Script/').read()
                print p
            print "END."
            break
        else:
            print "You type number out of range, try again.."
else:
	print "END."