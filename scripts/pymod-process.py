#!/usr/bin/env python
# dist: public

import sys, re, string, glob

DEFBUFLEN = 1024

# lots of precompiled regular expressions

# constants
re_pyconst_dir = re.compile(r'\s*/\* pyconst: (.*), "(.*)" \*/')

# callbacks
re_pycb_cbdef = re.compile(r'#define (CB_[A-Z_0-9]*)')
re_pycb_typedef = re.compile(r'typedef void \(\*([A-Za-z_0-9]*)\)')
re_pycb_dir = re.compile(r'/\* pycb: (.*?)(\*/)?$')

# interfaces
re_pyint_intdef = re.compile(r'#define (I_[A-Z_0-9]*)')
re_pyint_typedef = re.compile(r'typedef struct (I[a-z_0-9]*)')
re_pyint_func = re.compile(r'\t[A-Za-z].*?\(\*([A-Za-z_0-9]*)\)')
re_pyint_dir = re.compile(r'\s*/\* pyint: (.*?)(\*/)?$')
re_pyint_done = re.compile(r'^}')

# types
re_pytype_opaque = re.compile(r'/\* pytype: opaque: (.*), (.*) \*/')


# utility functions for constant translation

def const_int(n):
	const_file.write('INT(%s)\n' % n);

def const_string(n):
	const_file.write('STRING(%s)\n' % n);

def const_one(n):
	const_file.write('ONE(%s)\n' % n);

def const_callback(n):
	const_file.write('PYCALLBACK(%s)\n' % n);

def const_interface(n):
	const_file.write('PYINTERFACE(%s)\n' % n);


def tokenize_signature(s):
	out = []
	t = ''
	dash = 0
	for c in s:
		if ord(c) >= ord('a') and ord(c) <= ord('z'):
			t += c
		else:
			if t:
				out.append(t)
				t = ''

			if dash and c == '>':
				out.append('->')
				dash = 0
			else:
				dash = 0

			if c in ',()':
				out.append(c)
			elif c == '-':
				dash = 1

	assert not dash

	if t:
		out.append(t)

	return out

class Arg:
	def __init__(me, tp, flags):
		me.tp = tp
		me.flags = flags
	def __str__(me):
		return str(me.tp) + '[' + ', '.join(map(str, me.flags)) + ']'

class Func:
	def __init__(me, args, out):
		me.args = args
		me.out = out
	def __str__(me):
		return '(' + ', '.join(map(str, me.args)) + ' -> ' + str(me.out) + ')'

def is_func(o):
	return getattr(o, '__class__', None) is Func

def parse_arg(tokens):
	flags = []
	if tokens[0] == '(':
		tokens.pop(0)
		tp = parse_func(tokens)
		assert tokens[0] == ')'
		tokens.pop(0)
	else:
		tp = tokens.pop(0)
		while tokens and tokens[0] not in [',', '->', ')']:
			flags.append(tokens.pop(0))

	return Arg(tp, flags)

def parse_func(tokens):
	args = []
	while tokens[0] != '->':
		assert tokens[0] != ')'
		args.append(parse_arg(tokens))
		if tokens[0] == ',':
			tokens.pop(0)
	tokens.pop(0)
	out = parse_arg(tokens)
	return Func(args, out)


def genid(state = [100]):
	state[0] += 1
	return 'py_genid_%d' % state[0]

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
		return me.decl('')
	def buf_addr(me, s):
		return '&' + s
	def ptr_decl(me, s):
		return '%s*%s' % (me.decl(''), s)
	def conv_to_buf(me, buf, val):
		return '\t*%s = %s;' % (buf, val)
	def buf_init(me):
		return '0'
	def needs_free(me):
		return 0

class type_void(type_gen):
	def decl(me, s):
		return 'void ' + s

class type_null(type_gen):
	def format_char(me):
		return ''
	def decl(me, s):
		return 'void *%s = NULL' % s

class type_zero(type_gen):
	def format_char(me):
		return ''
	def decl(me, s):
		return 'int %s = 0' % s

class type_int(type_gen):
	def format_char(me):
		return 'i'
	def decl(me, s):
		return 'int ' + s

class type_double(type_gen):
	def format_char(me):
		return 'd'
	def decl(me, s):
		return 'double ' + s

class type_string(type_gen):
	def format_char(me):
		return 's'
	def decl(me, s):
		return 'const char *' + s
	def buf_decl(me):
		return 'charbuf'
	def ptr_decl(me, s):
		return 'char *%s' % s
	def buf_addr(me, s):
		return s
	def buf_init(me):
		return '{0}'
	def conv_to_buf(me, buf, val):
		return '\tastrncpy(%s, %s, buflen);' % (buf, val)

class type_zstring(type_gen):
	def format_char(me):
		return 'z'
	def decl(me, s):
		return 'const char *' + s

class type_player(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'Player *' + s
	def build_converter(me):
		return 'cvt_c2p_player'
	def parse_converter(me):
		return 'cvt_p2c_player'
	def buf_decl(me):
		return Exception()

class type_arena(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'Arena *' + s
	def build_converter(me):
		return 'cvt_c2p_arena'
	def parse_converter(me):
		return 'cvt_p2c_arena'
	def buf_decl(me):
		return Exception()

class type_config(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'ConfigHandle ' + s
	def build_converter(me):
		return 'cvt_c2p_config'
	def parse_converter(me):
		return 'cvt_p2c_config'
	def buf_decl(me):
		return Exception()

class type_banner(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'Banner *' + s
	def build_converter(me):
		return 'cvt_c2p_banner'
	def parse_converter(me):
		return 'cvt_p2c_banner'
	def buf_decl(me):
		return Exception()

class type_target(type_gen):
	def format_char(me):
		return 'O&'
	def decl(me, s):
		return 'const Target *' + s
	def build_converter(me):
		return 'cvt_c2p_target'
	def parse_converter(me):
		return 'cvt_p2c_target'
	def buf_decl(me):
		return Exception()
	def needs_free(me):
		return 1
	def conv_to_buf(me, buf, val):
		raise Exception()

class type_bufp(type_gen):
	def format_char(me):
		return 's#'
	def decl(me, s):
		raise Exception()
	def buf_decl(me):
		return 'const void *'
	def buf_init(me):
		return 'NULL'
	def conv_to_buf(me, buf, val):
		raise Exception()


def get_type(tp):
	try:
		cname = 'type_' + tp
		cls = globals()[cname]
		return cls()
	except:
		return None


def create_c_to_py_func(name, func, code, description):

	args = func.args
	out = func.out

	informat = []
	outformat = []
	inargs = []
	outargs = []
	decls = []
	extras1 = []
	extras2 = []
	extras3 = []
	allargs = []

	if out.tp == 'void':
		retorblank = ''
		rettype = type_void()
		defretval = ''
	else:
		assert not out.flags
		typ = get_type(out.tp)
		argname = 'ret'
		decls.append('\t%s;' % typ.decl(argname))
		outformat.append(typ.format_char())
		try:
			outargs.append(typ.build_converter())
		except:
			pass
		outargs.append('&%s' % argname)
		retorblank = 'ret'
		rettype = typ
		defretval = '0'

	idx = 0
	for arg in args:
		idx = idx + 1
		argname = 'arg%d' % idx

		opts = arg.flags
		typ = get_type(arg.tp)

		if arg.tp == 'void':
			continue

		elif arg.tp == 'clos':
			decls.append('\tPyObject *closobj = clos;')
			allargs.append('void *clos')

		elif 'in' in opts or not opts:
			# this is an incoming arg
			argname += '_in'
			informat.append(typ.format_char())
			try:
				inargs.append(typ.build_converter())
			except:
				pass
			inargs.append(argname)
			allargs.append(typ.decl(argname))

		elif 'out' in opts:
			# this is an outgoing arg
			argname += '_out'
			decls.append('\t%s;' % typ.decl(argname+'v'))
			outformat.append(typ.format_char())
			try:
				outargs.append(typ.parse_converter())
			except:
				pass
			outargs.append('&%sv' % argname)
			allargs.append(typ.ptr_decl(argname))
			extras3.append(typ.conv_to_buf(argname, argname+'v'))

		elif 'inout' in opts:
			# this is both incoming and outgoing
			argname += '_inout'
			decls.append('\t%s;' % typ.decl(argname+'v'))
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
			allargs.append(typ.ptr_decl(argname))
			extras3.append(typ.conv_to_buf('%s' % argname, '%sv' % argname))

		elif 'buflen' in opts:
			# this arg is a buffer length
			allargs.append('int %s' % argname)
			new = []
			for e in extras3:
				new.append(e.replace('buflen', argname))
			extras3 = new

	if inargs:
		inargs = ', ' + ', '.join(inargs)
	else:
		inargs = ''
	if outargs:
		outargs = ', ' + ', '.join(outargs)
	else:
		outargs = ''
	if allargs:
		allargs = ', '.join(allargs)
	else:
		allargs = 'void'
	informat = ''.join(informat)
	outformat = ''.join(outformat)
	decls = '\n'.join(decls)
	extras1 = '\n'.join(extras1)
	extras2 = '\n'.join(extras2)
	extras3 = '\n'.join(extras3)
	funcdecl = '%s(%s)' % (name, allargs)
	funcdecl = rettype.decl(funcdecl)
	code1 = """
local %(funcdecl)s
{
	PyObject *args, *out = NULL;
%(decls)s
	args = Py_BuildValue("(%(informat)s)"%(inargs)s);
	if (!args)
	{
		log_py_exception(L_ERROR, "python error building args for "
			"%(description)s");
		return %(defretval)s;
	}
%(extras1)s
%(code)s
	Py_DECREF(args);"""
	if outargs:
		code2 = """
	if (!out)
	{
		log_py_exception(L_ERROR, "python error calling "
			"%(description)s");
		return %(defretval)s;
	}
%(extras2)s
	if (!PyArg_ParseTuple(out, "%(outformat)s"%(outargs)s))
	{
		Py_XDECREF(out);
		log_py_exception(L_ERROR, "python error unpacking results of "
			"%(description)s");
		return %(defretval)s;
	}
%(extras3)s
	Py_XDECREF(out);
	return %(retorblank)s;
}
"""
	else:
		code2 = """
	Py_XDECREF(out);
}
"""

	return (code1 + code2) % vars()


def create_py_to_c_func(func):
	args = func.args
	out = func.out

	informat = []
	outformat = []
	inargs = []
	outargs = []
	decls = []
	extras1 = []
	extras2 = []
	extras3 = []
	extracode = []
	allargs = []

	if out.tp == 'void':
		asgntoret = ''
	else:
		assert not out.flags
		typ = get_type(out.tp)
		argname = 'ret'
		decls.append('\t%s;' % typ.decl(argname))
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

		opts = arg.flags

		typ = get_type(arg.tp)

		if arg.tp == 'void':
			continue

		elif arg.tp == 'formatted':
			# these are a little weird
			if idx != len(args):
				raise Exception, "formatted arg isn't last!"
			typ = get_type('string')
			argname += '_infmt'
			informat.append(typ.format_char())
			decls.append('\t%s;' % typ.decl(argname))
			inargs.append('&%s' % argname)
			allargs.append('"%s"')
			allargs.append('%s' % argname)

		elif arg.tp == 'clos':
			# hardcoded value. this depends on the existence of exactly
			# one function argument.
			allargs.append('cbfunc')

		elif is_func(arg.tp):
			cbfuncname = genid()
			code1 = create_c_to_py_func(cbfuncname, arg.tp,
					'\tout = PyObject_Call(closobj, args, NULL);',
					'callback function')
			extracode.append(code1)

			informat.append('O')
			decls.append('\tPyObject *cbfunc;')
			inargs.append('&cbfunc')
			allargs.append(cbfuncname)
			extras1.append("""
	if (!PyCallable_Check(cbfunc))
	{
		PyErr_SetString(PyExc_TypeError, "func isn't callable");
		return NULL;
	}
""")
			extras2.append('\tPy_DECREF(cbfunc);')

		elif 'in' in opts or not opts:
			# this is an incoming arg
			argname += '_in'
			informat.append(typ.format_char())
			decls.append('\t%s;' % typ.decl(argname))
			try:
				inargs.append(typ.parse_converter())
			except:
				pass
			if typ.needs_free():
				extras2.append('\tafree(%s);' % argname)
			if typ.format_char():
				inargs.append('&' + argname)
			allargs.append(argname)

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
			allargs.append(typ.buf_addr(argname))

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

		elif 'buflenout' in opts:
			# this is an outgoing arg, but it's a buffer length, so it's
			# treated differenly.
			argname += '_out'
			decls.append('\t%s %s = %s;' % (typ.buf_decl(), argname, typ.buf_init()))
			outargs.append('%s' % argname)
			allargs.append(typ.buf_addr(argname))

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
	extracode = '\n'.join(extracode)

	return vars()



# functions for asss<->python callback translation

pycb_cb_names = []

def translate_pycb(name, ctype, line):
	pycb_cb_names.append((name, ctype))

	tokens = tokenize_signature(line + '->void')
	func = parse_func(tokens)

	funcname = 'py_cb_%s' % name
	code = create_c_to_py_func(
		funcname, func,
		'\tcall_gen_py_callbacks(PYCBPREFIX %s, args);' % name,
		'callback %s' % name)
	callback_file.write(code)

	dict = create_py_to_c_func(func)
	dict.update(vars())
	if dict['extras1'] or dict['extras2'] or dict['extras3'] or \
	   dict['outformat'] or dict['outargs']:
		print "warning: %s: out or inout args not supported when " \
				"calling cbs from python" % name
	code = """
local void py_cb_call_%(name)s(Arena *arena, PyObject *args)
{
%(decls)s
	if (!PyArg_ParseTuple(args, "%(informat)s"%(inargs)s))
		return;
	DO_CBS(%(name)s, arena, %(ctype)s, (%(allargs)s));
}
""" % dict
	callback_file.write(code)


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
			try:
				tokens = tokenize_signature(thing)
				func = parse_func(tokens)
			except:
				print "couldn't parse '%s'" % thing
				continue

			dict = create_py_to_c_func(func)
			mthdname = 'pyint_method_%s_%s' % (iid, name)
			dict.update(vars())
			mthd = """
%(extracode)s
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
""" % dict
			methods.append(mthd)

			decl = """\
	{"%(name)s", (PyCFunction)%(mthdname)s, METH_VARARGS, NULL },
""" % vars()
			methoddecls.append(decl)


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
			try:
				tokens = tokenize_signature(thing)
				func = parse_func(tokens)
			except:
				print "bad declaration '%s'" % thing
				continue
			funcname = 'pyint_func_%s_%s' % (iid, name)
			code = create_c_to_py_func(funcname, func,
				"\tout = call_gen_py_interface(PYINTPREFIX %(iid)s, %(funcidx)d, args);" % vars(),
				'function %(name)s in interface %(iid)s' % vars())
			funcs.append(code)
			funcnames.append(funcname)

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


def handle_pyconst_directive(tp, pat):
	def clear(_):
		del pyconst_pats[:]

	pat = pat.replace('*', '[A-Z_0-9]*')

	if tp == 'enum':
		# these are always ints, and last until a line with a close-brace
		pat = r'\s*(%s)' % pat
		newre = re.compile(pat)
		pyconst_pats.append((newre, const_int))
		pyconst_pats.append((re.compile(r'.*}.*;.*()'), clear))
	elif tp.startswith('define'):
		# these can be ints or strings, and last until a blank line
		pat = r'#define (%s)' % pat
		newre = re.compile(pat)
		subtp = tp.split()[1].strip()
		func = { 'int': const_int, 'string': const_string, }[subtp]
		pyconst_pats.append((newre, func))
		pyconst_pats.append((re.compile(r'^$()'), clear))
	elif tp == 'config':
		pat = r'#define (%s)' % pat
		pyconst_pats.append((re.compile(pat + ' "'), const_string))
		pyconst_pats.append((re.compile(pat + ' [0-9]'), const_int))
		pyconst_pats.append((re.compile(pat + '$'), const_one))
		pyconst_pats.append((re.compile(r'/\* pyconst: config end \*/()'), clear))


def make_opaque_type(ctype, name):
	code = """
/* dummy value for uniqueness */
static int pytype_desc_%(name)s;

local PyObject * cvt_c2p_%(name)s(void *p)
{
	if (p == NULL)
	{
		Py_INCREF(Py_None);
		return Py_None;
	}
	else
		return PyCObject_FromVoidPtrAndDesc(p, &pytype_desc_%(name)s, NULL);
}

local int cvt_p2c_%(name)s(PyObject *o, void **pp)
{
	if (o == Py_None)
	{
		*pp = NULL;
		return TRUE;
	}
	else if (PyCObject_Check(o) &&
	         PyCObject_GetDesc(o) == &pytype_desc_%(name)s)
	{
		*pp = PyCObject_AsVoidPtr(o);
		return TRUE;
	}
	else
	{
		PyErr_SetString(PyExc_TypeError, "arg isn't a '%(name)s'");
		return FALSE;
	}
}
""" % vars()
	type_file.write(code)

	class mytype(type_gen):
		def format_char(me):
			return 'O&'
		def decl(me, s):
			return ctype + s
		def build_converter(me):
			return 'cvt_c2p_' + name
		def parse_converter(me):
			return 'cvt_p2c_' + name
		def buf_decl(me):
			return Exception()
		def conv_to_buf(me, buf, val):
			return Exception()

	globals()['type_' + name] = mytype


# output files
const_file = open('py_constants.inc', 'w')
callback_file = open('py_callbacks.inc', 'w')
int_file = open('py_interfaces.inc', 'w')
type_file = open('py_types.inc', 'w')

warning = """
/* THIS IS AN AUTOMATICALLY GENERATED FILE */
"""

const_file.write(warning)
callback_file.write(warning)
int_file.write(warning)
type_file.write(warning)

lines = []
for pat in sys.argv[1:]:
	for f in glob.glob(pat):
		lines.extend(open(f).readlines())

# default constants
const_string('ASSSVERSION')
const_string('BUILDDATE')
const_int('ASSSVERSION_NUM')

const_int('TRUE')
const_int('FALSE')

init_pyint()

pyconst_pats = []

# now process file
intdirs = []
lastfunc = ''

for l in lines:
	# constants
	m = re_pyconst_dir.match(l)
	if m:
		handle_pyconst_directive(m.group(1), m.group(2))

	for myre, func in pyconst_pats:
		m = myre.match(l)
		if m:
			func(m.group(1))

	# callbacks
	m = re_pycb_cbdef.match(l)
	if m:
		lastcbdef = m.group(1)
	m = re_pycb_typedef.match(l)
	if m:
		lasttypedef = m.group(1)
	m = re_pycb_dir.match(l)
	if m:
		const_callback(lastcbdef)
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
			const_interface(lastintdef)
			translate_pyint(lastintdef, lasttypedef, intdirs)
			intdirs = []

	# types
	m = re_pytype_opaque.match(l)
	if m:
		make_opaque_type(m.group(1).strip(), m.group(2).strip())


finish_pycb()
finish_pyint()

