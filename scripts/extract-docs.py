#!/usr/bin/env python

import sys, re, string


re_helptext = re.compile(r"^local helptext_t ([a-z]*)_help =$")
re_crap = re.compile(r'"?(.*?)(\\n)?"?;?$')
re_targets = re.compile(r"Targets: (.*)")
re_args = re.compile(r"Args: (.*)")
re_module = re.compile(r"Module: (.*)")
re_braces = re.compile(r"({.*?})")


def rem_crap(l):
	m = re_crap.match(l)
	if m:
		return m.group(1)
	else:
		return l


checks = [
	(re_targets, 'targets'),
	(re_args, 'args'),
	(re_module, 'requiremod')
]

def print_line(line):
	# replace < and > with math symbols
	line = line.replace('<', '$<$')
	line = line.replace('>', '$>$')

	# replace braces with texttt expressions
	line = re_braces.sub(r"\\texttt\1", line)

	# handle underscores and hash marks
	line = line.replace('_', '\_')
	line = line.replace('#', '\#')

	# check itemized lists
	if line.startswith(' * '):
		return '\\item ' + line + '\n'

	# now check for targets/args/module
	for ex, repl in checks:
		m = ex.match(line)
		if m:
			return '\\%s{%s}\n' % (repl, m.group(1))

	# otherwise, just plain text
	return line + '\n'


def print_doc(cmd, text):
	initem = 0
	out = ''
	out += "\\command{%s}\n" % cmd
	for line in text:
		if line.startswith(' * '):
			line = '\\item ' + line[3:]
			if not initem:
				out += '\\begin{itemize}\n'
				initem = 1
		elif initem:
			out += '\\end{itemize}\n'
			initem = 0
		out += print_line(line)
	if initem:
		out += '\\end{itemize}\n'
	out += '\n'
	return out


def extract_docs(lines):
	docs = {}
	i = 0
	while i < len(lines):
		l = lines[i]

		m = re_helptext.match(l)
		if m:
			# found a command
			cmdname = m.group(1)

			# extract lines until one ends in ;
			text = []
			i = i + 1
			while not lines[i].endswith(';'):
				text.append(lines[i])
				i = i + 1
			text.append(lines[i])
			i = i + 1

			# remove crap
			text = map(rem_crap, text)
			
			# output docs
			docs[cmdname] = print_doc(cmdname, text)

		i = i + 1

	return docs


if __name__ == '__main__':
	lines = map(string.strip, sys.stdin.readlines())
	docs = extract_docs(lines).items()
	docs.sort()
	for c, t in docs:
		sys.stdout.write(t)

