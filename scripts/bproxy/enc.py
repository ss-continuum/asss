
# enc.py
# vie encryption routines, in python
# dist: public

import array

# edata is None | int * int array


# this means no encryption
noenc = None


def gen_keystream(key):
	# key 0 means no encryption
	if key == 0:
		return noenc
	# minor hacks to work around python's integer semantics
	elif key > 0x7fffffff:
		key = -int(0x100000000-key)

	origkey = key

	tab = []
	for l in range(0, 260):
		t = (key * 0x834E0B5F) >> 32
		t = (t+key) >> 16
		t = t + (t>>31)
		t = ((((((t * 9) << 3) - t) * 5) << 1) - t) << 2
		rem = key % 0x1F31D
		if key < 0:
			rem = rem - 0x1F31D
		key = ((rem * 0x41A7) - t) + 0x7B
		if key == 0 or key < 0:
			key = key + 0x7fffffff
		tab.append(key & 0xffff)

	arr = array.array('H')
	arr.fromlist(tab)
	str = arr.tostring()
	arr = array.array('i')
	arr.fromstring(str)
	return (origkey, arr)


def do_stuff(edata, pkt, do_work):
	# test zero key
	if not edata:
		return pkt

	origlen = len(pkt)

	# strip off type byte(s)
	if pkt[0] == '\0':
		prepend = pkt[:2]
		mydata = pkt[2:]
	else:
		prepend = pkt[:1]
		mydata = pkt[1:]

	# pad with zeros
	pad = (4 - (len(mydata) & 3)) & 3
	mydata = mydata + '\0' * pad

	# get into array
	arr = array.array('i')
	arr.fromstring(mydata)

	# do the work
	do_work(edata, arr)

	# put back into string
	out = prepend + arr.tostring()
	return out[:origlen]


def encrypt(edata, pkt):
	def enc_work(edata, arr):
		work, table = edata
		for i in range(len(arr)):
			work = arr[i] ^ table[i] ^ work
			arr[i] = work
	return do_stuff(edata, pkt, enc_work)


def decrypt(edata, pkt):
	def dec_work(edata, arr):
		work, table = edata
		for i in range(len(arr)):
			tmp = arr[i]
			arr[i] = table[i] ^ work ^ tmp
			work = tmp
	return do_stuff(edata, pkt, dec_work)


### testing stuff
#
# import sys
# k = long(sys.argv[1])
# s = gen_keystream(k)
# #_, str = s
# #sys.stdout.write(str.tostring())
#
# lst = []
# for i in range(32):
# 	lst.append(i)
# test = array.array('B')
# test.fromlist(lst)
# test = test.tostring()
#
# sys.stdout.write(encrypt(s, test))
# sys.stdout.write(decrypt(s, test))
#
### to test:
### for a in 1 2 3 7777 -4554 2882382797 4008636142 4294967295; do diff <(../../test/genkey $a) <(python enc.py $a | od -t x1 -A n | sed 's/^ //'); done

