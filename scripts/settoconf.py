#!/usr/bin/env python
# dist: public

import sys

file = sys.stdin

lines = file.readlines()

data = {}

for l in lines:
	f = l.strip().split(':', 5)

	while len(f) < 6:
		f.append('')

	sec, key, val, min, max, desc = f

	s = data.get(sec, {})
	s[key] = val
	data[sec] = s

for sec, vals in data.items():
	print '\n[%s]' % sec
	for key, val in vals.items():
		print '%s = %s' % (key, val)

print

