#!/usr/bin/env python

import sys, os, select, signal, optparse
import util, prot, ui, pilot

conns = []

def set_signal():
	def sigfunc(signum, frame):
		util.log("caught signal, disconnecting")
		for conn, mypilot in conns:
			conn.disconnect()
		os._exit(0)
	signal.signal(signal.SIGTERM, sigfunc)
	signal.signal(signal.SIGINT, sigfunc)


def main():
	parser = optparse.OptionParser()
	parser.add_option('-s', '--server', type='string', dest='server', default='127.0.0.1')
	parser.add_option('-p', '--port', type='int', dest='port', default=5000)
	parser.add_option('-n', type='int', dest='n', default=1)

	(opts, args) = parser.parse_args()

	set_signal()

	for i in range(opts.n):
		conn = prot.Connection()
		conn.connect(opts.server, opts.port)
		mypilot = pilot.Pilot(conn, name='loadgen-%02d-%03d' % (os.getpid() % 99, i))
		conns.append((conn, mypilot))

	myui = None

	while conns:

		socks = [0]
		for conn, mypilot in conns:
			socks.append(conn.sock)

		try:
			ready, _, _ = select.select(socks, [], [], 0.01)
		except:
			ready = []

		# read/process some data
		for conn, mypilot in conns:
			if conn.sock in ready:
				try:
					conn.try_read()
				except prot.Disconnected:
					conns.remove((conn, mypilot))
					continue
			if myui and 0 in ready:
				line = sys.stdin.readline().strip()
				myui.process_line(line)

			# try sending
			try:
				conn.try_sending_outqueue()
			except prot.Disconnected:
				conns.remove((conn, mypilot))
				continue

			# move pilot
			mypilot.iter()


if __name__ == '__main__':
	main()


