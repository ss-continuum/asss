#!/usr/bin/env python

import sys, os, time, random, select, signal, optparse
import util, prot, ui, pilot, timer

conns = []

def set_signal():
	def sigfunc(signum, frame):
		util.log("caught signal, disconnecting")
		for conn, mypilot in conns:
			conn.disconnect()
		os._exit(0)
	signal.signal(signal.SIGTERM, sigfunc)
	signal.signal(signal.SIGINT, sigfunc)


def new_conn(name = None):
	conn = prot.Connection()
	conn.connect(opts.server, opts.port)
	dest = random.randint(0, opts.arenas - 1)
	mypilot = pilot.Pilot(conn, name=name, defarena=dest)
	conns.append((conn, mypilot))


def login_event():
	if len(conns) == 1:
		add = 1
	elif len(conns) >= 2 * opts.n:
		add = 0
	else:
		add = random.random() > 0.5
	if add:
		new_conn()
		print "*** new connection -> %d" % len(conns)
	else:
		cp = random.choice(conns)
		conns.remove(cp)
		cp[0].disconnect()
		print "*** dropping connection -> %d" % len(conns)


def arena_event():
	conn, mypilot = random.choice(conns)
	if mypilot.pid is not None:
		dest = random.randint(0, opts.arenas - 1)
		print "*** arena change pid %d -> arena %d" % (mypilot.pid, dest)
		mypilot.goto_arena(dest)


def main():
	parser = optparse.OptionParser()
	parser.add_option('-s', '--server', type='string', dest='server', default='127.0.0.1')
	parser.add_option('-p', '--port', type='int', dest='port', default=5000)
	parser.add_option('-n', '--num', type='int', dest='n', default=1)
	parser.add_option('-a', '--arenas', type='int', dest='arenas', default=1)
	parser.add_option('-L', '--loginiv', type='int', dest='loginiv', default=0)
	parser.add_option('-A', '--arenaiv', type='int', dest='arenaiv', default=0)

	global opts
	(opts, args) = parser.parse_args()

	set_signal()

	for i in range(opts.n):
		new_conn('loadgen-%02d-%03d' % (os.getpid() % 99, i))

	myui = None

	mytimers = timer.Timers()
	if opts.loginiv:
		mytimers.add(opts.loginiv, login_event)
	if opts.arenaiv:
		mytimers.add(opts.arenaiv, arena_event)

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

		mytimers.iter()


if __name__ == '__main__':
	main()


