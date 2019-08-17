Commands on Laptop:
- Router:
  - Wifi: PEREKRESTOK 88888888
  - Router IP: 192.168.88.1
  - Router admin page: router.asus.com
  - Admin: light12345
- Find all PRi's on the network:
  ./find_all.sh
- Find all PRi's on the network and update the list on disk:
  ./find_all.sh >all_pi
- Remote reboot (or uncomment other commands inside):
  ./reboot.sh 192.168.88.227
  ./reboot.sh all
- SSH to a RPi:
  sshpass -p "raspberry" ssh pi@192.168.88.227
- Remote reboot:
  sshpass -p "raspberry" ssh pi@192.168.88.227 sudo reboot
- Some other scripts exist to copy "client" binary fro one RPi to Laptop
  and then to copy "client" to all RPi's

System commands on RPi:
- Show system time: date
- Show HW time: sudo hwclock -r
- Write system time to HW time: sudo hwclock -w
- Reboot: sudo reboot
- Boot-time client startup config: sudo vi /etc/rc.local
- Wifi config: sudo vi /etc/wpa_supplicant/wpa_supplicant.conf
- Boot-time WH-to-system clock sync: sudo vi /lib/udev/hwclock-set
- View system log: less /var/log/syslog
- The config file that got corrupted for unknown reason: /boot/config.txt
  We keep a backup copy of the same in /home/pi/boot_config.txt
- To run a hzeller's matrix demo in Adafruit mode:
  cd rpi-rgb-led-matrix
  ./examples-api-use/demo -D 5 --led-gpio-mapping=adafruit-hat --led-chain=10
- To run a hzeller's matrix demo in Chinese mode:
  cd rpi-rgb-led-matrix
  ./examples-api-use/demo -D 5 --led-gpio-mapping=adafruit-hat --led-chain=10 --led-multiplexing=2 --led-rgb-sequence=BGR

Client commands on RPi (change to the Git directory first: `cd controller`):
- Kill background client:
  ./kill_all.sh
- Rebuild code:
  ./rebuild_all.sh
- Start client from command line:
  sudo ./client ./
- View or update configuration (need to reboot, or restart client):
  vi config.txt
- Debug client from command line:
  sudo gdb ./client ./
- Force lights to show a particular scenario (from command line):
  ./client ./ lsd

Git commands:
* Make a new commit
  git commit -a
* Push commit to remote server
  git push origin HEAD:master
* Fetch latest code
  git fetch origin && git checkout origin/master

Card backup:
  sudo dd if=/dev/sdXXX bs=1M iflag=fullblock count=3000 | gzip > ./rpi_3g.gz.dd

Card restore:
  gzip -dc ./rpi_3g.gz.dd | sudo dd of=/dev/sdXXX bs=1M

New card provisioning (doesn't have HW clock info): SD_SETUP.txt
