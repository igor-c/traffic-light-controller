import paramiko
import getopt, sys

ipList = []
gifList = []
fromPath = ''
toPath = ''

# fullCmdArguments = sys.argv
# argumentList = fullCmdArguments[1:]
# paramiko.util.log_to_file('paramiko.log')

try:
	opts, args = getopt.getopt(argv,"i:f:t:a:",["ip=","from=","to=","animation="])
except getopt.GetoptError:
	print 'pySFTP.py -f <copy from> -t <copy to> -i <client ip addr> -a <animation (gif) file>'
	sys.exit(2)
for opt, arg in opts:
	if opt in ("-f", "--from"):
		fromPath=arg
	elif opt in ("-t", "--to"):
		toPath=arg
	elif opt in ("-i", "--ip"):
		ipList.append(arg)
	elif opt in ("-a", "--animation"):
		gifList.append(arg)


port = 22
password = "raspberry"
username = "pi"

for i, host in enumerate(ipList):
	transport = paramiko.Transport((host, port))
	transport.connect(username = username, password = password)
	sftp = paramiko.SFTPClient.from_transport(transport)

	for j, gif in enumerate(gifList):
		sftp.put(fromPath+gif, toPath+gif)

	sftp.close()
	transport.close()