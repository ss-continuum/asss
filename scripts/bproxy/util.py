
import sys, time

# utility stuff

logfile = None


def ticks():
	return long(time.time() * 100)


def open_logfile(f):
	try:
		logfile = open(f, 'a', 1)
	except:
		log("can't open log file")


def log(s):
	l = '%s %s\n' % (time.ctime()[4:], s)
	sys.stderr.write('bproxy: ' + l)
	if logfile:
		logfile.write(l)


