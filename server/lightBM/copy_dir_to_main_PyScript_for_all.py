import paramiko

dir_to_script = '@python /mnt/usbel/RPipyControl/screen.py'
#dir_to_script         = '@python screen.py'
#autostart_file = '/etc/xdg/lxsession/LXDE-pi/autostart'
autostart_file = '/home/pi/.config/lxsession/LXDE-pi/autostart'

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

print "This script will set dir to main PyScript in " + autostart_file
print "Press Y or N to continue"
while True:
    char = getch()
    if char.upper() in ("Y", "N"):
        break

if char=="Y":
	print "BEGIN.."
	for x in range(11,25):
		print ('192.168.1.'+str(x))
		ssh = paramiko.SSHClient()
		ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
		ssh.connect('192.168.1.'+str(x), username='root', password='raspberry')

		ftp = ssh.open_sftp()
		file=ftp.file(autostart_file, "r", -1)
		lines = file.readlines()
		ftp.close()

		ftp = ssh.open_sftp()
		file=ftp.file(autostart_file, "w+", -1)
		for line in lines:
			lenght=0-len("#key for auto replace script")-1
			end_str=line[lenght:-1]
			if end_str!="#key for auto replace script":
				file.write(line)
				file.flush()
		ftp.close()

		ftp = ssh.open_sftp()
		file=ftp.file(autostart_file, "a", -1)
		file.write(dir_to_script +' #key for auto replace script\n')
		file.flush()
		ftp.close()

		print "-----------"
		ssh.close()
	print "END."
else:
	
	print "END."

