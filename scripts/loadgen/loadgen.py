

import sys, select, signal

import util, prot, ui, pilot


def set_signal(conn):
	def sigfunc(signum, frame):
		util.log("caught signal, disconnecting")
		conn.disconnect()
	signal.signal(signal.SIGTERM, sigfunc)
	signal.signal(signal.SIGINT, sigfunc)


def main(argv):

	# reasonable defaults
	server = '127.0.0.1'
	port = 5000

	conn = prot.Connection()
	conn.connect(server, port)

	set_signal(conn)

	mypilot = pilot.Pilot(conn)

	myui = None
	#myui = ui.UI(mypilot)

	while 1:

		# do a select 10 times a second
		try:
			ready, _, _ = select.select([conn.sock, 0], [], [], 0.1)
		except:
			ready = []

		# read/process some data
		if conn.sock in ready:
			conn.try_read()
		if myui and 0 in ready:
			line = sys.stdin.readline().strip()
			myui.process_line(line)

		# try sending
		conn.try_sending_outqueue()

		# move pilot
		mypilot.iter()



if __name__ == '__main__':
	main(sys.argv)

