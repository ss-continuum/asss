
import sys, struct

import util
log = util.log


s_nothing = 0
s_sentkey = 1
s_connected = 2

class Disconnected(Exception): pass

class Pkt:
	def __init__(me, data, seqnum = None):
		me.data = data
		me.lastretry = 0
		me.reliable = (ord(data[0]) == 0 and ord(data[1]) == 3)
		if seqnum is not None:
			me.seqnum = seqnum
		elif me.reliable:
			(me.seqnum,) = struct.unpack('<I', data[2:6])
		else:
			me.seqnum = None


class Connection:

	def __init__(me):
		# relatively constant
		me.reliable_retry = 100 # 1 second
		me.max_bigpkt = 512 * 1024
		me.timeoutinterval = 1500

		# connection state
		me.c2sn = 0
		me.s2cn = 0

		me.sock = None

		me.lastrecv = util.ticks()

		me.outq = []
		me.inq = {}

		me.stage = s_nothing
		me.sentconnect = 0

		me.dispatch = {}
		me.curchunk = ''


	def raw_send(me, d):
		r = me.sock.send(d)
		if r < len(d):
			log('send failed to send whole packet (%d < %d bytes)'
					% (r, len(d)))


	def try_read(me):
		try:
			r = me.sock.recv(512)
			if r:
				me.lastrecv = util.ticks()
				me.process_pkt(r)
				me.try_process_inqueue()
			else:
				log("recvd 0 bytes from remote socket")

		except:
			# probably ewouldblock
			raise
			pass



	def try_sending_outqueue(me):
		now = util.ticks()

		# check for disconnect
		if (now - me.lastrecv) > me.timeoutinterval:
			log("no packets from server in %s seconds, assuming down" %
				((now-me.lastrecv)/100))
			raise Disconnected()

		# only send stuff once we're connected
		if me.stage != s_connected:
			return

		for p in me.outq:
			if (now - p.lastretry) > me.reliable_retry:
				me.raw_send(p.data)
				if p.reliable:
					p.lastretry = now
				else:
					me.outq.remove(p)


	def send(me, data, rel = 0):
		if rel:
			pkt = Pkt(struct.pack('<BBI', 0, 3, me.c2sn) + data, me.c2sn)
			me.c2sn += 1
		else:
			pkt = Pkt(data)

		me.outq.append(pkt)


	def handle_ack(me, sn):
		for p in me.outq:
			if sn == p.seqnum:
				me.outq.remove(p)


	def add_handler(me, type, func):
		me.dispatch[type] = func

	def del_handler(me, type):
		del me.dispatch[type]


	def process_pkt(me, p):
		#log("got pkt: %s" % repr(p))
		t1 = ord(p[0])

		if t1 == 0:
			t2 = ord(p[1])
			if t2 == 2:
				# key response
				me.stage = s_connected

				# set up encryption
				(key,) = struct.unpack('<i', p[2:6])
				if key != 0:
					log("remote server responded with bad key")
					raise Disconnected()
				else:
					log("remote server responded correctly")

				# respond
				func = me.dispatch.get(0x0200)
				if func: func(p)
			elif t2 == 3:
				# reliable
				pkt = Pkt(p)
				ack = struct.pack('<BBI', 0, 4, pkt.seqnum)
				me.raw_send(ack)
				if pkt.seqnum >= me.s2cn:
					me.inq[pkt.seqnum] = pkt
			elif t2 == 4:
				# ack
				(sn,) = struct.unpack('<I', p[2:6])
				me.handle_ack(sn)
			elif t2 == 7:
				# disconnect
				# close our sockets and die
				log("got disconnect from server, exiting")
				me.sock.close()
				# the main loop catches this and removes us
				raise Disconnected()
			elif t2 == 8:
				# chunk
				me.curchunk = me.curchunk + p[2:]
				if len(me.curchunk) > me.max_bigpkt:
					log("big packet too long. discarding.")
					me.curchunk = ''
			elif t2 == 9:
				# chunk tail
				me.curchunk = me.curchunk + p[2:]
				me.process_pkt(me.curchunk)
				me.chrchunk = ''
			elif t2 == 10:
				# presize
				log("got presized packet from remote server")
			elif t2 == 14:
				# grouped
				pos = 2
				while pos < len(p):
					l = ord(p[pos])
					pos += 1
					newp = p[pos:pos+l]
					me.process_pkt(newp)
					pos += l
			else:
				log("unknown network subtype: %d" % t2)

		else:
			func = me.dispatch.get(t1)
			if func:
				func(p)
			else:
				#log("unknown packet type: %d" % t1)
				pass


	def try_process_inqueue(me, ):
		while 1:
			pkt = me.inq.get(me.s2cn)
			if pkt:
				del me.inq[me.s2cn]
				me.s2cn += 1
				d = pkt.data[6:]
				try:
					me.process_pkt(d)
				except:
					raise
					log("error processing packet from inqueue, type %x" % ord(d[0]))
			else:
				return


	def get_inq_len(me):
		return len(me.inq)


	def connect(me, ip, port):
		import socket

		me.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		me.sock.connect((ip, port))
		me.sock.setblocking(0)

		log("contacting remote server")

		key = 0 # force no encryption
		pkt = struct.pack('< BB I BB', 0, 1, key, 1, 0)
		me.raw_send(pkt)
		me.stage = s_sentkey


	def disconnect(me, count = 1):
		log("sending disconnect")
		pkt = struct.pack('<BB', 0, 7)
		for ii in range(count):
			me.raw_send(pkt)


