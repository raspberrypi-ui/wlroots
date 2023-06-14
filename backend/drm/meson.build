hwdata = dependency(
	'hwdata',
	required: 'drm' in backends,
	native: true,
	not_found_message: 'Required for the DRM backend.',
)

if not (hwdata.found())
	subdir_done()
endif

libliftoff_rpi = dependency(
	'libliftoff_rpi',
	version: '>=0.0.1',
	fallback: ['libliftoff_rpi', 'liftoff_rpi'],
	required: 'drm' in get_option('backends')
)

hwdata_dir = hwdata.get_variable(pkgconfig: 'pkgdatadir')
pnpids_c = custom_target(
	'pnpids.c',
	output: 'pnpids.c',
	input: files(hwdata_dir / 'pnp.ids'),
	feed: true,
	capture: true,
	command: files('gen_pnpids.sh'),
)
wlr_files += pnpids_c

wlr_files += files(
	'atomic.c',
	'backend.c',
	'cvt.c',
	'drm.c',
	'legacy.c',
	'monitor.c',
	'properties.c',
	'renderer.c',
	'util.c',
)

if libliftoff_rpi.found()
	wlr_files += files('libliftoff.c')
endif

features += { 'drm-backend': true }
internal_features += { 'libliftoff_rpi': libliftoff_rpi.found() }
wlr_deps += libliftoff_rpi
