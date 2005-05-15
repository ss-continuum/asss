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
				certs[cur_certname] = \
						base64.decodestring(cur_data).strip()
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

entries = {}

heads = run('automate heads', BRANCH).splitlines()

ancestors = run('automate ancestors', *heads).splitlines()
every = int(len(ancestors)/80)

for n, rev in enumerate(ancestors):
	certs = parse_certs(run('certs', rev))
	if '@' not in certs['author'] and certs['author'].startswith('d'):
		certs['author'] = 'grelminar@yahoo.com'
	certs['rev'] = rev

	entry = """\
%(date)s    %(author)s    %(branch)s    %(rev)s

%(changelog)s


""" % certs
	entries[certs['date']] = entry

	# simple progress display
	if n % every == 0:
		sys.stderr.write('=')
sys.stderr.write('\n')

entries = entries.items()
entries.sort()
entries.reverse()
for _, entry in entries:
	sys.stdout.write(entry)

