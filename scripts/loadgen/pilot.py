
import os, struct, random

import util
log = util.log
from pkt_types import *


def make_ppk(rot, x, y, xspeed, yspeed, status, bty, nrg):
	tm = int(util.ticks() & 0x7fffffff)
	ppk1 = struct.pack('< B b I h h',
		C2S_POSITION, rot, tm, xspeed, y)
	ppk2 = struct.pack('< b h h H h 2x',
		status, x, yspeed, bty, nrg)
	wpn = ''
	epd = ''
	cksum = 0
	for c in ppk1:
		cksum ^= ord(c)
	for c in ppk2:
		cksum ^= ord(c)
	return ppk1 + chr(cksum) + ppk2 + wpn + epd


class Timer:
	def __init__(me, iv, func):
		me.interval = iv
		me.func = func
		me.last = util.ticks() + iv * random.random()


class Pilot:
	def __init__(me, conn, name = None, pwd = ''):
		me.conn = conn

		me.conn.add_handler(0x0200, me.handle_connected)
		me.conn.add_handler(S2C_WHOAMI, me.handle_whoami)
		me.conn.add_handler(S2C_ENTERINGARENA, me.handle_inarena)
		me.conn.add_handler(S2C_LOGINRESPONSE, me.handle_loginresponse)
		me.conn.add_handler(S2C_WEAPON, me.handle_weapon)
		me.conn.add_handler(S2C_POSITION, me.handle_position)
		me.conn.add_handler(S2C_CHAT, me.handle_chat)

		if name:
			me.name = name
		else:
			me.name = 'loadgen-%06d' % random.randint(0, 999999)
		me.pwd = pwd

		me.x = me.y = 512<<4

		me.reset_stats()

		me.timers = []

		me.add_timer(500, me.print_stats)

	def add_timer(me, iv, func):
		me.timers.append(Timer(iv, func))

	def iter(me):
		now = util.ticks()
		for t in me.timers:
			if (now - t.last) > t.interval:
				t.last = now
				t.func()

	def handle_connected(me, pkt):
		log("sending login packet")
		login = struct.pack('< B x 32s 32s I x 2x 2x h 8x 4x 12x',
			C2S_LOGIN, me.name, me.pwd, 0x12345678, 134)
		me.conn.send(login, 1)

	def handle_loginresponse(me, pkt):
		log("got login response, entering arena")
		goarena = struct.pack('< B B 2x h h h 16s',
			C2S_GOTOARENA, 0, 1024, 768, 0, '')
		me.conn.send(goarena, 1)

	def handle_whoami(me, pkt):
		(me.pid,) = struct.unpack('< x H', pkt)
		log("i have pid %d" % me.pid)

	def handle_inarena(me, pkt):
		log("in arena, starting ppks")
		me.add_timer(10 + 10*random.random(), me.send_ppk)
		me.add_timer(200 + 50*random.random(), me.send_chat)

	def handle_weapon(me, pkt):
		#log("got weapon")
		me.wpn_rcvd += 1

	def handle_position(me, pkt):
		#log("got position")
		me.pos_rcvd += 1

	def handle_chat(me, pkt):
		#log("got chat msg")
		me.chat_rcvd += 1

	def send_ppk(me):
		#log("sending ppk: (%4d, %4d)" % (me.x, me.y))
		me.pos_sent += 1
		bty = random.choice([20, 40, 60, 100, 200, 280, 500])
		ppk = make_ppk(0, me.x, me.y, 0, 0, 0, bty, 1700)
		me.conn.send(ppk)

		# simple random walk, for now:
		me.x += random.choice([-10, 10])
		me.y += random.choice([-10, 10])
		if me.x < 0 or me.x > 65535:
			me.x = 512<<4
		if me.y < 0 or me.y > 65535:
			me.y = 512<<4

	def send_chat(me):
		me.chat_sent += 1
		pkt = struct.pack('< B B x h', C2S_CHAT, 2, -1)
		pkt += 'msg from %d' % me.pid
		pkt += chr(0)
		me.conn.send(pkt, 1)

	def print_stats(me):
		log("pos_sent=%2d  pos_rcvd=%4d  wpn_rcvd=%3d  \
chat_sent=%2d  chat_rcvd=%3d  inq_len=%2d" % \
			(me.pos_sent, me.pos_rcvd, me.wpn_rcvd,
			me.chat_sent, me.chat_rcvd, me.conn.get_inq_len()))
		me.reset_stats()

	def reset_stats(me):
		me.pos_sent = me.pos_rcvd = me.wpn_rcvd = 0
		me.chat_sent = me.chat_rcvd = 0


