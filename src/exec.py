
import asss

env = {}
env.update(asss.__dict__)

def c_py(cmd, params, p, targ):
	"""\
Module: <py> exec
Targets: any
Args: <python code>
Executes arbitrary python code. The code runs in a namespace containing
all of the asss module, plus three helpful pre-set variables: {me} is
yourself, {t} is the target of the command, and {a} is the current
arena. You can write multi-line statements by separating lines with
semicolons. Be sure to get the indentation correct.
"""
	params = params.replace(';', '\n')
	try:
		env['me'] = p
		env['t'] = targ
		env['a'] = p.arena
		exec params in env
	finally:
		del env['me']
		del env['t']
		del env['a']

ref = asss.add_command('py', c_py)

