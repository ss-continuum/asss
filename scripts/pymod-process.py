#!/usr/bin/env python
# dist: public

import sys, re, string

DEFBUFLEN = 1024

# lots of precompiled regular expressions

# constants
re_int = re.compile(r'#define (I_[A-Z_0-9]*)')
re_cb = re.compile(r'#define (CB_[A-Z_0-9]*)')
re_msg = re.compile(r'#define (MSG_[A-Z_0-9]*)')
re_caps = re.compile(r'#define (CAP_[A-Z_0-9]*)')
re_log = re.compile(r'#define (L_[A-Z_0-9]*)')
re_mm1 = re.compile(r'#define (MM_[A-Z_0-9]*)')
re_cfg_str = re.compile(r'#define (CFG_[A-Z_0-9]*) "')
re_cfg_int = re.compile(r'#define (CFG_[A-Z_0-9]*) [0-9]')
re_cfg_one = re.compile(r'#define (CFG_[A-Z_0-9]*)$')

re_actions = re.compile(r'\t([PA_0-9]A_[A-Z]*)')
re_ball = re.compile(r'\t(BALL_[A-Z_0-9]*)')
re_flag = re.compile(r'\t(FLAG_[A-Z_0-9]*)')
re_billing = re.compile(r'\t(BILLING_[A-Z_0-9]*)')
re_auth = re.compile(r'\t(AUTH_[A-Z_0-9]*)')
re_sound = re.compile(r'\t(SOUND_[A-Z_0-9]*)')
re_prize = re.compile(r'\t(PRIZE_[A-Z_0-9]*)')
re_mm2 = re.compile(r'\t(MM_[A-Z_0-9]*)')
re_persist = re.compile(r'\t(PERSIST_[A-Z_0-9]*)')
re_type = re.compile(r'\t(T_[A-Z_0-9]*)')
re_status = re.compile(r'\t(S_[A-Z_0-9]*)')
re_stat = re.compile(r'\t(STAT_[A-Z_0-9]*)')
re_iv = re.compile(r'\t(INTERVAL_[A-Z_0-9]*)')
re_turf = re.compile(r'\t(TR_[A-Z_0-9]*)')

# callbacks
re_pycb_cbdef = re.compile(r'#define (CB_[A-Z_0-9]*)')
re_pycb_typedef = re.compile(r'typedef void \(\*([A-Za-z_0-9]*)\)')
re_pycb_dir = re.compile(r'/\* pycb: (.*?)(\*/)?$')

# interfaces
re_pyint_intdef = re.compile(r'#define (I_[A-Z_0-9]*)')
re_pyint_typedef = re.compile(r'typedef struct (I[a-z_0-9]*)')
re_pyint_func = re.compile(r'\t[A-Za-z].*\(\*([A-Za-z_0-9]*)\)')
re_pyint_dir = re.compile(r'\s*/\* pyint: (.*?)(\*/)?$')
re_pyint_done = re.compile(r'^}')


# utility functions for constant translation

def const_int(n):
	const_file.write('INT(%s)\n' % n);

def const_string(n):
	const_file.write('STRING(%s)\n' % n);

def const_one(n):
	const_file.write('ONE(%s)\n' % n);

def const_callback(n):
	const_file.write('CALLBACK(%s)\n' % n);

def const_interface(n):
	const_file.write('CALLBACK(%s)\n' % n);


class type_gen:
	def format_char(me):
		raise Exception()
	# these two should throw exceptions unless the format char is O&
	def build_converter(me):
		raise Exception()
	def parse_converter(me):
		raise Exception()
	def decl(me):
		raise Exception()
	def buf_decl(me):
		return me.decl()
	def buf_cast(me):
		return ''
	def conv_to_buf(me, buf, val):
		return '\t*%s = %s;' % (buf, val)
	def buf_init(me):
		return '0'
	def needs_free(me):
		return 0

class type_int(type_gen):
	def format_char(me):
		return 'i'
	def decl(me):
		return 'int'

class type_string(type_gen):
	def format_char(me):
		return 's'
	def decl(me):
		return 'const char *'
	def buf_decl(me):
		return 'charbuf'
	def buf_init(me):
		return '{0}'
	def buf_cast(me):
		return '(char *)'
	def conv_to_buf(me, buf, val):
		return '\tastrncpy(%s, %s, buflen);' % (buf, val)

class type_formatted(type_gen):
	def format_char(me):
		return 's'
	def decl(me):
		return 'const char *'

class type_player(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me):
		return 'Player *'
	def build_converter(me):
		return 'cvt_c2p_player'
	def parse_converter(me):
		return 'cvt_p2c_player'
	def buf_decl(me):
		return Exception()

class type_arena(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me):
		return 'Arena *'
	def build_converter(me):
		return 'cvt_c2p_arena'
	def parse_converter(me):
		return 'cvt_p2c_arena'
	def buf_decl(me):
		return Exception()

class type_banner(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me):
		return 'Banner *'
	def build_converter(me):
		return 'cvt_c2p_banner'
	def parse_converter(me):
		return 'cvt_p2c_banner'
	def buf_decl(me):
		return Exception()

class type_balldata(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me):
		return 'struct BallData *'
	def build_converter(me):
		return 'cvt_c2p_balldata'
	def parse_converter(me):
		return 'cvt_p2c_balldata'
	def buf_decl(me):
		return Exception()
	def needs_free(me):
		return 1
	def conv_to_buf(me, buf, val):
		return 'FIXME'

class type_target(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me):
		return 'const Target *'
	def build_converter(me):
		return 'cvt_c2p_target'
	def parse_converter(me):
		return 'cvt_p2c_target'
	def buf_decl(me):
		return Exception()
	def needs_free(me):
		return 1
	def conv_to_buf(me, buf, val):
		return 'FIXME'


def get_type(tp):
	cname = 'type_' + tp
	cls = globals()[cname]
	return cls()


# functions for asss<->python callback translation

pycb_cb_names = []

def translate_pycb(name, ctype, line):
	pycb_cb_names.append((name, ctype))

	fields = map(string.strip, line.split(','))
	format = []
	inargs = []
	outargs = []
	idx = 0
	for f in fields:
		if not f:
			continue
		idx = idx + 1
		typ = get_type(f)
		format.append(typ.format_char())
		inargs.append('%s arg%d' % (typ.decl(), idx))
		try:
			outargs.append(typ.build_converter())
		except:
			pass
		outargs.append('arg%d' % idx)

	if inargs:
		inargs = ', '.join(inargs)
	else:
		inargs = 'void'
	if outargs:
		outargs = ',\n\t\t' + ', '.join(outargs)
	else:
		outargs = ''
	format = ''.join(format)
	func = """
local void py_cb_%(name)s(%(inargs)s)
{
	PyObject *args;
	args = Py_BuildValue("(%(format)s)"%(outargs)s);
	if (args) {
		call_gen_py_callbacks(PYCBPREFIX %(name)s, args);
		Py_DECREF(args);
	}
}

""" % vars()
	callback_file.write(func)


	decls = []
	asgns = []
	inargs = []
	outargs = []
	idx = 0
	for f in fields:
		if not f:
			continue
		idx = idx + 1
		typ = get_type(f)
		decls.append('\t%s arg%d;' % (typ.decl(), idx))
		try:
			inargs.append(typ.parse_converter())
			inargs.append('(void*)&arg%d' % idx)
		except:
			inargs.append('&arg%d' % idx)
		outargs.append('arg%d' % idx)
	decls = '\n'.join(decls)
	asgns = '\n'.join(asgns)
	if inargs:
		inargs = ', ' + ', '.join(inargs)
	else:
		inargs = ''
	outargs = ', '.join(outargs)
	func = """
local void py_cb_call_%(name)s(Arena *arena, PyObject *args)
{
%(decls)s
	if (!PyArg_ParseTuple(args, "%(format)s"%(inargs)s))
		return;
	DO_CBS(%(name)s, arena, %(ctype)s, (%(outargs)s));
}
""" % vars()
	callback_file.write(func)


def finish_pycb():
	callback_file.write("""
typedef void (*py_cb_caller)(Arena *arena, PyObject *args);
local HashTable *py_cb_callers;

local void init_py_callbacks(void)
{
	py_cb_callers = HashAlloc();
""")
	for n, ctype in pycb_cb_names:
		callback_file.write("	{ %s typecheck = py_cb_%s; (void)typecheck; }\n" % (ctype, n))
		callback_file.write("	mm->RegCallback(%s, py_cb_%s, ALLARENAS);\n" % (n, n))
		callback_file.write("	HashReplace(py_cb_callers, PYCBPREFIX %s, py_cb_call_%s);\n" % (n, n))
	callback_file.write("""\
}

local void deinit_py_callbacks(void)
{
""")
	for n, _ in pycb_cb_names:
		callback_file.write("	mm->UnregCallback(%s, py_cb_%s, ALLARENAS);\n" % (n, n))
	callback_file.write("""\
	HashFree(py_cb_callers);
}
""")


# translating interfaces asss<->python

def init_pyint():
	out = int_file
	out.write("""
/* pyint declarations */

typedef char charbuf[%s];

local void pyint_generic_dealloc(pyint_generic_interface_object *self)
{
	mm->ReleaseInterface(self->i);
	PyObject_Del(self);
}

""" % DEFBUFLEN)

pyint_init_code = []

def translate_pyint(iid, ifstruct, dirs):

	_, allowed = dirs.pop(0)
	allowed = map(string.strip, allowed.split(','))
	if 'use' in allowed:

		objstructname = 'pyint_obj_%s' % iid

		methods = []
		methoddecls = []
		members = []
		memberdecls = []

		for name, thing in dirs:
			argsout = thing.split('->')
			if len(argsout) == 1:
				# this is a member
				pass
			elif len(argsout) == 2:
				# this is a method
				args, out = argsout
				args = map(string.strip, args.split(','))
				if 'void' in args:
					args.remove('void')
				out = string.strip(out)
				informat = []
				outformat = []
				inargs = []
				outargs = []
				decls = []
				extras1 = []
				extras2 = []
				extras3 = []
				allargs = []

				if out == 'void':
					asgntoret = ''
				else:
					typ = get_type(out)
					argname = 'ret'
					decls.append('\t%s %s;' % (typ.decl(), argname))
					outformat.append(typ.format_char())
					try:
						outargs.append(typ.build_converter())
					except:
						pass
					outargs.append(argname)
					asgntoret = '%s = ' % argname

				idx = 0
				for arg in args:
					idx = idx + 1
					argname = 'arg%d' % idx

					opts = arg.split(' ')
					tname = opts.pop(0)

					typ = get_type(tname)

					if tname == 'formatted':
						# these are a little weird
						if idx != len(args):
							raise Exception, "formatted arg isn't last!"
						argname += '_infmt'
						informat.append(typ.format_char())
						decls.append('\t%s %s;' % (typ.decl(), argname))
						inargs.append('&%s' % argname)
						allargs.append('"%s"')
						allargs.append('%s' % argname)

					elif 'in' in opts or not opts:
						# this is an incoming arg
						argname += '_in'
						informat.append(typ.format_char())
						decls.append('\t%s %s;' % (typ.decl(), argname))
						try:
							inargs.append(typ.parse_converter())
						except:
							pass
						if typ.needs_free():
							extras2.append('\tafree(%s);' % argname)
						inargs.append('&%s' % argname)
						allargs.append('%s' % argname)

					elif 'out' in opts:
						# this is an outgoing arg
						argname += '_out'
						outformat.append(typ.format_char())
						decls.append('\t%s %s = %s;' % (typ.buf_decl(), argname, typ.buf_init()))
						try:
							outargs.append(typ.build_converter())
						except:
							pass
						outargs.append('%s' % argname)
						allargs.append('%s&%s' % (typ.buf_cast(), argname))

					elif 'inout' in opts:
						# this is both incoming and outgoing
						argname += '_inout'
						informat.append(typ.format_char())
						outformat.append(typ.format_char())
						decls.append('\t%s %s = %s;' % (typ.buf_decl(), argname, typ.buf_init()))
						try:
							inargs.append(typ.parse_converter())
						except:
							pass
						inargs.append('&%s' % argname)
						try:
							outargs.append(typ.build_converter())
						except:
							pass
						if typ.needs_free():
							extras3.append('\tafree(%s);' % argname)
						outargs.append('%s' % argname)
						allargs.append('&%s' % argname)

					elif 'buflen' in opts:
						# this arg is a buffer length
						# it must be passed in, so it's sort of like an
						# inarg, but it doesn't get parsed.
						allargs.append('%s' % DEFBUFLEN)


				decls = '\n'.join(decls)
				if inargs:
					inargs = ', ' + ', '.join(inargs)
				else:
					inargs = ''
				if outargs:
					outargs = ', ' + ', '.join(outargs)
				else:
					outargs = ''
				informat = ''.join(informat)
				outformat = ''.join(outformat)
				allargs = ', '.join(allargs)
				extras1 = '\n'.join(extras1)
				extras2 = '\n'.join(extras2)
				extras3 = '\n'.join(extras3)

				mthdname = 'pyint_method_%s_%s' % (iid, name)
				mthd = """
local PyObject *
%(mthdname)s(%(objstructname)s *me, PyObject *args)
{
	PyObject *out;
%(decls)s
	if (!PyArg_ParseTuple(args, "%(informat)s"%(inargs)s))
		return NULL;
%(extras1)s
	%(asgntoret)sme->i->%(name)s(%(allargs)s);
%(extras2)s
	out = Py_BuildValue("%(outformat)s"%(outargs)s);
%(extras3)s
	return out;
}
""" % vars()
				methods.append(mthd)

				decl = """\
	{"%(name)s", (PyCFunction)%(mthdname)s, METH_VARARGS, NULL },
""" % vars()
				methoddecls.append(decl)

			else:
				print "bad interface directive: %s" % thing


		objstructdecl = """
typedef struct {
	PyObject_HEAD
	%(ifstruct)s *i;
} %(objstructname)s;
""" % vars()
		methoddeclname = 'pyint_%s_methods' % iid
		methoddeclstart = """
local PyMethodDef %(methoddeclname)s[] = {
""" % vars()
		methoddeclend = """\
	{NULL}
};
""" % vars()
		memberdeclname = 'pyint_%s_members' % iid
		memberdeclstart = """
local PyMemberDef %(memberdeclname)s[] = {
""" % vars()
		memberdeclend = """\
	{NULL}
};
""" % vars()
		typestructname = 'pyint_%s_type' % iid
		typedecl = """
local PyTypeObject %(typestructname)s = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
	"asss.%(ifstruct)s",       /*tp_name*/
	sizeof(%(objstructname)s), /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	(destructor)pyint_generic_dealloc, /*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	0,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	0,                         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,        /*tp_flags*/
	0,                         /*tp_doc */
	0,                         /*tp_traverse */
	0,                         /*tp_clear */
	0,                         /*tp_richcompare*/
	0,                         /*tp_weaklistoffset*/
	0,                         /*tp_iter*/
	0,                         /*tp_iternext*/
	%(methoddeclname)s,        /*tp_methods*/
	%(memberdeclname)s,        /*tp_members*/
	/* rest are null */
};
""" % vars()
		doinit = """\
	if (PyType_Ready(&%(typestructname)s) < 0) return;
	HashReplace(pyint_ints, %(iid)s, &%(typestructname)s);
""" % vars()
		pyint_init_code.append(doinit)

		int_file.write('\n/* using interface %(iid)s from python {{{ */\n' % vars())
		int_file.write(objstructdecl)
		for m in methods:
			int_file.write(m)
		int_file.write(methoddeclstart)
		for m in methoddecls:
			int_file.write(m)
		int_file.write(methoddeclend)
		for m in members:
			int_file.write(m)
		int_file.write(memberdeclstart)
		for m in memberdecls:
			int_file.write(m)
		int_file.write(memberdeclend)
		int_file.write(typedecl)
		int_file.write('\n/* }}} */\n')

	if 'impl' in allowed:

		ifacename = 'pyint_int_%s' % iid

		funcs = []
		funcnames = []
		lastout = None

		funcidx = -1
		for name, thing in dirs:
			funcidx = funcidx + 1
			argsout = thing.split('->')
			if len(argsout) == 1:
				# this is a member
				pass
			elif len(argsout) == 2:
				# this is a method
				args, out = argsout
				args = map(string.strip, args.split(','))
				if 'void' in args:
					args.remove('void')
				out = string.strip(out)
				informat = []
				outformat = []
				inargs = []
				outargs = []
				decls = []
				extras1 = []
				extras2 = []
				extras3 = []
				allargs = []

				if out == 'void':
					retorblank = ''
					rettype = 'void'
					defretval = ''
				else:
					typ = get_type(out)
					argname = 'ret'
					decls.append('\t%s %s;' % (typ.decl(), argname))
					outformat.append(typ.format_char())
					try:
						outargs.append(typ.build_converter())
					except:
						pass
					outargs.append('&%s' % argname)
					retorblank = 'ret'
					rettype = typ.decl()
					defretval = '0'

				idx = 0
				for arg in args:
					idx = idx + 1
					argname = 'arg%d' % idx

					opts = arg.split(' ')
					tname = opts.pop(0)

					typ = get_type(tname)

					if 'in' in opts or not opts:
						# this is an incoming arg
						argname += '_in'
						informat.append(typ.format_char())
						try:
							inargs.append(typ.build_converter())
						except:
							pass
						inargs.append('%s' % argname)
						allargs.append('%s %s' % (typ.decl(), argname))

					elif 'out' in opts:
						# this is an outgoing arg
						argname += '_out'
						decls.append('\t%s %sv;' % (typ.decl(), argname))
						outformat.append(typ.format_char())
						try:
							outargs.append(typ.parse_converter())
						except:
							pass
						outargs.append('&%sv' % argname)
						allargs.append('%s *%s' % (typ.buf_decl(), argname))
						extras3.append(typ.conv_to_buf('%s%s' % (typ.buf_cast(), argname),
							'%sv' % argname))

					elif 'inout' in opts:
						# this is both incoming and outgoing
						argname += '_inout'
						decls.append('\t%s %sv;' % (typ.decl(), argname))
						informat.append(typ.format_char())
						outformat.append(typ.format_char())
						try:
							inargs.append(typ.parse_converter())
						except:
							pass
						inargs.append('*%s' % argname)
						try:
							outargs.append(typ.build_converter())
						except:
							pass
						outargs.append('&%sv' % argname)
						allargs.append('%s *%s' % (typ.buf_decl(), argname))
						extras3.append(typ.conv_to_buf('%s' % argname, '%sv' % argname))

					elif 'buflen' in opts:
						# this arg is a buffer length
						allargs.append('int %s' % argname)
						new = []
						for e in extras3:
							new.append(e.replace('buflen', argname))
						extras3 = new

				decls = '\n'.join(decls)
				if inargs:
					inargs = ', ' + ', '.join(inargs)
				else:
					inargs = ''
				if outargs:
					outargs = ', ' + ', '.join(outargs)
				else:
					outargs = ''
				informat = ''.join(informat)
				outformat = ''.join(outformat)
				allargs = ', '.join(allargs)
				extras1 = '\n'.join(extras1)
				extras2 = '\n'.join(extras2)
				extras3 = '\n'.join(extras3)

				funcname = 'pyint_func_%s_%s' % (iid, name)
				func = """
local %(rettype)s
%(funcname)s(%(allargs)s)
{
	PyObject *args, *out;
%(decls)s
	args = Py_BuildValue("(%(informat)s)"%(inargs)s);
	if (!args)
	{
		log_py_exception(L_ERROR, "python error building args for "
			"function %(name)s in interface %(iid)s");
		return %(defretval)s;
	}
%(extras1)s
	out = call_gen_py_interface(PYINTPREFIX %(iid)s, %(funcidx)d, args);
	Py_DECREF(args);
	if (!out)
	{
		log_py_exception(L_ERROR, "python error calling "
			"function %(name)s in interface %(iid)s");
		return %(defretval)s;
	}
%(extras2)s
	if (!PyArg_ParseTuple(out, "%(outformat)s"%(outargs)s))
	{
		Py_XDECREF(out);
		log_py_exception(L_ERROR, "python error unpacking results of "
			"function %(name)s in interface %(iid)s");
		return %(defretval)s;
	}
%(extras3)s
	Py_XDECREF(out);
	return %(retorblank)s;
}
""" % vars()
				funcs.append(func)
				funcnames.append(funcname)

			else:
				print "bad interface directive: %s" % thing

		funcnames = ',\n\t'.join(funcnames)
		ifstructdecl = """
local struct %(ifstruct)s %(ifacename)s = {
	INTERFACE_HEAD_INIT(%(iid)s, "pyint-%(iid)s")
	%(funcnames)s
};

""" % vars()
		init = "\tHashReplace(pyint_impl_ints, PYINTPREFIX %(iid)s, &%(ifacename)s);\n" % vars()
		pyint_init_code.append(init)

		int_file.write('\n/* implementing interface %(iid)s in python {{{ */\n' % vars())
		for func in funcs:
			int_file.write(func)
		int_file.write(ifstructdecl)
		int_file.write('\n/* }}} */\n')



def finish_pyint():
	out = int_file
	out.write("""
local HashTable *pyint_ints;
local HashTable *pyint_impl_ints;

local void init_py_interfaces(void)
{
	pyint_ints = HashAlloc();
	pyint_impl_ints = HashAlloc();
""")
	for line in pyint_init_code:
		out.write(line)
	out.write("""\
}

local void deinit_py_interfaces(void)
{
""")
	out.write("""\
	HashFree(pyint_ints);
	HashFree(pyint_impl_ints);
}
""")


# output files
const_file = open('py_constants.inc', 'w')
callback_file = open('py_callbacks.inc', 'w')
int_file = open('py_interfaces.inc', 'w')

warning = """
/* THIS IS AN AUTOMATICALLY GENERATED FILE */
"""

const_file.write(warning)
callback_file.write(warning)
int_file.write(warning)


lines = sys.stdin.readlines()

# default constants
const_string('ASSSVERSION')
const_string('BUILDDATE')
const_int('ASSSVERSION_NUM')

const_int('TRUE')
const_int('FALSE')

for ship in ['WARBIRD', 'JAVELIN', 'SPIDER', 'LEVIATHAN', 'TERRIER',
		'WEASEL', 'LANCASTER', 'SHARK', 'SPEC']:
	const_int(ship)

init_pyint()

# now process file
intdirs = []
lastfunc = ''

for l in lines:
	# constants
	for r in [re_caps, re_cfg_str]:
		m = r.match(l)
		if m:
			const_string(m.group(1))
	for r in [re_msg, re_actions, re_ball, re_flag, re_billing, re_auth,
			re_sound, re_prize, re_log, re_mm1, re_mm2, re_cfg_int,
			re_persist, re_type, re_status, re_stat, re_iv, re_turf]:
		m = r.match(l)
		if m:
			const_int(m.group(1))
	m = re_cfg_one.match(l)
	if m:
		const_one(m.group(1))
	m = re_cb.match(l)
	if m:
		const_callback(m.group(1))
	m = re_int.match(l)
	if m:
		const_interface(m.group(1))

	# callbacks
	m = re_pycb_cbdef.match(l)
	if m:
		lastcbdef = m.group(1)
	m = re_pycb_typedef.match(l)
	if m:
		lasttypedef = m.group(1)
	m = re_pycb_dir.match(l)
	if m:
		translate_pycb(lastcbdef, lasttypedef, m.group(1))

	# interfaces
	m = re_pyint_intdef.match(l)
	if m:
		lastintdef = m.group(1)
		intdirs = []
	m = re_pyint_typedef.match(l)
	if m:
		lasttypedef = m.group(1)
	m = re_pyint_func.match(l)
	if m:
		lastfunc = m.group(1)
	m = re_pyint_dir.match(l)
	if m:
		intdirs.append((lastfunc, m.group(1)))
	m = re_pyint_done.match(l)
	if m:
		if intdirs:
			translate_pyint(lastintdef, lasttypedef, intdirs)
			intdirs = []

finish_pycb()
finish_pyint()

