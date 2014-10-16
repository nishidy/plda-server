#!/usr/bin/python

import sys
import socket
import ssl
from optparse import OptionParser
from time import time

def main():

	usage="usage"
	parser=OptionParser(usage)
	parser.add_option("-f","--file",dest="filename",help="read data from FILENAME")
	parser.add_option("-i","--dest_ip",dest="dest_ip",type="string",default="127.0.0.1",help="connect to DEST_IP")
	parser.add_option("-p","--dest_port",dest="dest_port",type="int",default=5328,help="connect to DEST_PORT")
	parser.add_option("-v","--verbose",action="store_true",default=False,help="verbose")
	parser.add_option("-c","--cert_file",dest="cert_file",help="Server certification file", metavar="FILE")

	(options, args) = parser.parse_args()

	if options.filename == None:
		parser.error("Need to specify -f with filename to read")
		quit(99)

	server = options.dest_ip
	port   = options.dest_port

	clientsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	ssl_clientsock = ssl.wrap_socket(clientsock)

	ssl_clientsock.connect((server,port))

	rcvbuf = 1024
	for line in open(options.filename,"r"):

		stime=float(time())
		
		ssl_clientsock.sendall(line)

		rcvmsg=""
		while True:
			tmpmsg=ssl_clientsock.recv(rcvbuf)
			rcvmsg += tmpmsg
			if len(tmpmsg) < rcvbuf: break

		etime=float(time())

		if options.verbose:
			print "Received from server[%s] (# of topics is %d): %s" %\
					(server,len(rcvmsg.split()),rcvmsg)
			print "%.3f sec elapsed to get the result." % (etime-stime)


	ssl_clientsock.close()


if __name__ == "__main__":
	main()


