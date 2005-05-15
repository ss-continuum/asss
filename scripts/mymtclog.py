#!/usr/bin/env python

import sys, os, base64

BRANCH = 'asss.asss.main'

def run(*words):
	return os.popen('monotone ' + ' '.join(words)).read()

def parse_certs(certdata):
	certs = {}
	cur_certname = ''
	cur_signer = ''
	cur_data = ''
	state = 'nocert'
	for l in certdata.splitlines():
		l = l.strip()
		if state == 'nocert':
			assert l.startswith('[rcert ')
			state = 'getcertname'
		elif state == 'getcertname':
			cur_certname = l
			state = 'getsigner'
		elif state == 'getsigner':
			cur_signer = l
			state = 'getdata'
		elif state == 'getdata':
			if l.endswith(']'):
				cur_data += l[:-1]
				certs[cur_certname] = base64.decodestring(cur_data)
				cur_certname = ''
				cur_signer = ''
				cur_data = ''
				state = 'getsig'
			else:
				cur_data += l
		elif state == 'getsig':
			if l == '[end]':
				state = 'nocert'
		else:
			assert not 'uh oh'

	assert state == 'nocert'
	return certs

queue = [run('automate heads', BRANCH)]

while queue:
	rev = queue.pop(0)
	certs = parse_certs(run('certs', rev))
	for parent in run('automate parents', rev).splitlines():
		if parent not in queue:
			queue.append(parent)

	if '@' not in certs['author'] and certs['author'].startswith('d'):
		certs['author'] = 'grelminar@yahoo.com'

	print """\
%(date)s    %(author)s

%(changelog)s
""" % certs

	# simple progress display
	print >>sys.stderr, certs['date'], '\r',

