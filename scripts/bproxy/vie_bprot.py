
# vie_bprot.py
# vie billing server protocol

import sys, struct, re

import asss_bprot
from util import ticks, log


# constants
reliable_retry = 100 # ticks
max_bigpkt = 512 * 1024

s_nothing = 0
s_sentkey = 1
s_connected = 2


# globals
c2sn = 0
s2cn = 0
sock = None

stage = s_nothing


def set_sock(s):
	global sock
	sock = s


class Pkt:
	def __init__(me, data, seqnum = None):
		me.data = data
		me.lastretry = ticks()
		if seqnum != None:
			me.seqnum = seqnum
		else:
			(me.seqnum,) = struct.unpack('<I', data[2:6])


def raw_send(d):
	global sock
	r = sock.send(d)
	if r < len(d):
		log('send failed to send whole packet (%d < %d bytes)'
				% (r, len(d)))


def try_read():
	global sock
	try:
		r = sock.recv(512)
		if r:
			try:
				process_pkt(r)
			except:
				log("error processing recvd packet, type %x" % r[0])
			try_process_inqueue()
		else:
			log("recvd 0 bytes from remote socket")

	except:
		# probably ewouldblock
		pass



def try_sending_outqueue():
	global outq

	# only send stuff once we're connected
	if stage != s_connected:
		return

	now = ticks()

	for p in outq:
		if (ticks - p.lastretry) > reliable_retry:
			raw_send(p.data)


def queue_pkt(data):
	global outq

	pkt = Pkt(struct.pack('<BBI', 0, 3, c2sn) + data, c2sn)
	c2sn = c2sn + 1

	outq.append(pkt)


def handle_ack(sn):
	for p in outq:
		if sn == p.seqnum:
			outq.remove(p)


def handle_b2s_authresponse(flag, pid, name, pwd, usage, year,
			month, day, hour, minute, second, billerid):

	first = '%d-%d-%d %d:%2d:%2d' % (day, month, year, hour, minute, second)
	# more...
	if flag == 0:
		send_b2s_pok(pid, '', name, squad, billerid, usage, first)
	else:
		send_b2s_pbad(pid, 'error in login')


re_msg = re.compile(  r':#([^:]*):\((.*?)\)>(.*)'  )

def handle_b2s_genmessage(str):
	m = re_msg.match(str)
	if m:
		dest, sender, msg = m.groups()
		if dest[0] == '#':
			asss_bprot.send_b2s_rmtsqd(dest[1:], sender, 0, msg)
		else:
			asss_bprot.send_b2s_rmt(dest, sender, 0, msg)
	else:
		# broadcast message
		asss_bprot.send_b2s_broadcast('', 0, msg)


def handle_b2s_singlemsg(pid, str):
	asss_bprot.send_b2s_msg(pid, 0, str)


lastchatmsg = ''

def handle_b2s_chat(pid, chan, str):
	if str != lastchatmsg:
		sender, msg = str.split('> ')
		asss_bprot.send_b2s_chattxt('', sender, 0, msg)
		lastchatmsg = str

	asss_bprot.send_b2s_chat(pid, chan)



B2S_AUTHRESPONSE           = 0x01
B2S_SHUTDOWN               = 0x02
B2S_GENMESSAGE             = 0x03
B2S_RECYCLE                = 0x04
B2S_KICKUSER               = 0x08
B2S_SINGLEMSG              = 0x09
B2S_CHAT                   = 0x0A

curchunk = ''

def process_pkt(p):
	global curchunk

	t1 = ord(p[0])

	if t1 == 0:
		t2 = ord(p[1])
		if t2 == 2:
			# key response
			stage = s_connected
			log("remote server responded")
		elif t2 == 3:
			# reliable
			pkt = Pkt(p)
			inq.append(pkt)
			ack = struct.pack('<BBI', 0, 4, pkt.seqnum)
			raw_send(ack)
		elif t2 == 4:
			# ack
			(sn,) = struct.unpack('<I', p.data[2:6])
			handle_ack(sn)
		elif t2 == 7:
			# disconnect
			# close our sockets and die
			log("got disconnect from server, exiting")
			sock.close()
			sys.exit(0)
		elif t2 == 8:
			# chunk
			curchunk = curchunk + p[2:]
			if len(curchunk) > max_bigpkt:
				log("big packet too long. discarding.")
				curchunk = ''
		elif t2 == 9:
			# chunk tail
			curchunk = curchunk + p[2:]
			process_pkt(curchunk)
			chrchunk = ''
		elif t2 == 10:
			# presize
			log("got presized packet from remote server")
		elif t2 == 14:
			# grouped
			log("got grouped packet from remote server")
		else:
			log("unknown network subtype: %d" % t2)

	elif t1 == B2S_AUTHRESPONSE:
		flag, pid, name, pwd, bnr, usage, year, month, day, hour, \
			minute, second, billerid = \
			struct.unpack('< x B i 24s 24s 96s i 6h 4x i 4x', p)
		handle_b2s_authresponse(flag, pid, name, pwd, usage, year,
			month, day, hour, minute, second, billerid)

	elif t1 == B2S_SHUTDOWN:
		# '< B x 4x 4x'
		log("got b2s shutdown message")

	elif t1 == B2S_GENMESSAGE:
		# '< B 4x 2x' # string
		str = p[7:]
		handle_b2s_genmessage(str)

	elif t1 == B2S_RECYCLE:
		# '< B x 4x 4x'
		log("got b2s recycle message")

	elif t1 == B2S_KICKUSER:
		# '< B i i'
		log("got b2s kickuser message")

	elif t1 == B2S_SINGLEMSG:
		# '< B i' # string
		(pid,) = struct.unpack('< x i', p[0:5])
		str = p[5:]
		handle_b2s_singlemsg(pid, str)

	elif t1 == B2S_CHAT:
		# '< B i B' # string
		pid, chan = struct.unpack('< x i B', p[0:6])
		str = p[6:]
		handle_b2s_chat(pid, chan, str)

	else:
		log("unknown packet type: %d" % t1)


def try_process_inqueue():
	global inq

	for p in inq:
		if p.seqnum == s2cn:
			s2cn = s2cn + 1
			try:
				process_pkt(p.data)
			except:
				log("error processing packet from inqueue, type %x" % p.data[0])
			inq.remove(p)


# sending stuff

S2B_KEEPALIVE              = 0x01
S2B_LOGIN                  = 0x02
S2B_LOGOFF                 = 0x03
S2B_PLAYERLOGIN            = 0x04
S2B_PLAYERLEAVING          = 0x05
S2B_REMOTEPRIV             = 0x07
S2B_REGDATA                = 0x0D
S2B_LOGMESSAGE             = 0x0E
S2B_WARNING                = 0x0F
S2B_BANNER                 = 0x10
S2B_STATUS                 = 0x11
S2B_SZONEMSG               = 0x12
S2B_COMMAND                = 0x13
S2B_CHATMSG                = 0x14

def send_s2b_login(serverid, groupid, scoreid, name, pw):
	pkt = struct.pack('< B i i i 128s 32s', S2B_LOGIN, serverid,
	groupid, scoreid, name, pw)
	queue_pkt(pkt)
	sentconnect = 1

def send_s2b_logout():
	pkt = struct.pack('< B', S2B_LOGOUT)
	queue_pkt(pkt)

def send_s2b_playerlogin(flag, ipaddy, name, pw, pid, macid, timezone, contid = None):
	pkt = struct.pack('< B B I 32s 32s i i h i', S2B_PLAYERLOGIN, flag,
	ipaddy, name, pw, pid, macid, timezone)
	if contid:
		pkt = pkt + contid
	queue_pkt(pkt)

def send_s2b_playerleave(pid):
	pkt = struct.pack('< B i', S2B_PLAYERLEAVING, pid)
	queue_pkt(pkt)

def send_s2b_remotepriv(pid, text):
	pkt = struct.pack('< B i i 2x', S2B_REMOTEPRIV, pid, groupid)
	pkt = pkt + text + '\0'
	queue_pkt(pkt)

def send_s2b_logmessage(pid, targetpid, text):
	pkt = struct.pack('< B i i', S2B_LOGMESSAGE, pid, targetpid)
	pkt = pkt + text + '\0'
	queue_pkt(pkt)

def send_s2b_warning(pid, text):
	pkt = struct.pack('< B i', S2B_WARNING, pid)
	pkt = pkt + text + '\0'
	queue_pkt(pkt)

# not sure what this is:
# struct S2BStatus
# {
# 	u8 type; /* S2B_STATUS */
# 	i32 pid;
# 	i32 unknown[4];
# };
# // '< B i 4i'

def send_s2b_command(pid, text):
	pkt = struct.pack('< B i', S2B_COMMAND, pid)
	pkt = pkt + text + '\0'
	queue_pkt(pkt)

def send_s2b_chat(pid, channel, text):
	pkt = struct.pack('< B i 32s', S2B_CHATMSG, pid, channel)
	pkt = pkt + text + '\0'
	queue_pkt(pkt)


def connect():
	log("contacting remote server")
	key = 0
	pkt = struct.pack('< BB I BB', 0, 1, key, 1, 0)
	raw_send(pkt)
	stage = s_sentkey


def disconnect():
	# first send the nice log off packet
	if sentconnect:
		send_s2b_logout()
		try_sending_outqueue()

	# then send the low level disconnect
	pkt = struct.pack('<BB', 0, 7)
	raw_send(pkt)
	raw_send(pkt)


