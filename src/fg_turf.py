
# dist: public

import asss

flagcore = asss.get_interface(asss.I_FLAGCORE)
mapdata = asss.get_interface(asss.I_MAPDATA)
cfg = asss.get_interface(asss.I_CONFIG)

KEY_TURF_OWNERS = 20


# flag game

def init(a):
	# get settings
	a.fg_turf_fc = fc = mapdata.GetFlagCount(a)
	a.fg_turf_persist = cfg.GetInt(a.cfg, "Flag", "PersistentTurfOwners", 1)

	# set up turf game
	flagcore.SetCarryMode(a, asss.CARRY_NONE)
	flagcore.ReserveFlags(a, fc)

	# set up initial flags
	f = asss.flaginfo()
	f.state = asss.FI_ONMAP
	f.freq = -1
	for i in range(fc):
		flagcore.SetFlags(a, i, f)

def flagtouch(a, p, fid):
	n, f = flagcore.GetFlags(a, fid)
	assert n == 1
	oldfreq = f.freq
	f.state = asss.FI_ONMAP
	newfreq = f.freq = p.freq
	flagcore.SetFlags(a, fid, f)
	asss.call_callback(asss.CB_TURFTAG, (a, p, fid, oldfreq, newfreq), a)

def cleanup(a, fid, reason, carrier, freq):
	pass

myflaggame = (init, flagtouch, cleanup)


# persistent stuff

def get_ownership(a):
	if getattr(a, 'fg_turf_persist', 0):
		owners = []
		for i in range(a.fg_turf_fc):
			f = flagcore.GetFlags(a, i)
			owners.append(f.freq)
		return owners
	else:
		return None

def set_ownership(a, owners):
	# only set owners from persistent data if the setting says so, and
	# if the setting agrees:
	if getattr(a, 'fg_turf_persist', 0) and len(owners) == a.fg_turf_fc:
		f = asss.flaginfo()
		f.state = asss.FI_ONMAP
		for i in range(a.fg_turf_fc):
			f.freq = owners[i]
			flagcore.SetFlags(a, i, f)

def clear_ownership(a):
	pass # the init action does this already

my_apd_ref = asss.reg_arena_persistent(
		KEY_TURF_OWNERS, asss.INTERVAL_FOREVER_NONSHARED, asss.PERSIST_ALLARENAS,
		get_ownership, set_ownership, clear_ownership)


# attaching/detaching

def mm_attach(a):
	a.fg_turf_ref = asss.reg_interface(asss.I_FLAGGAME, myflaggame, a)

def mm_detach(a):
	for attr in ['ref', 'fc', 'persist']:
		try: delattr(a, 'fg_turf_' + attr)
		except: pass

