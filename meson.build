project('libratbag', 'c',
	version : '0.14',
	license : 'MIT/Expat',
	default_options : [ 'c_std=gnu99', 'warning_level=2' ],
	meson_version : '>= 0.40.0')

# The DBus API version. Increase this every time the DBus API changes.
# No backwards/forwards guarantee, clients are expected to understand
# whatever ratbagd speaks or bail out. This should be removed if we ever
# finish the API and declare it stable.
ratbagd_api_version = 1

# We use libtool-version numbers because it's easier to understand.
# Before making a release, the libratbag_so_* and liblur_so_*
# numbers should be modified. The components are of the form C:R:A.
# a) If binary compatibility has been broken (eg removed or changed interfaces)
#    change to C+1:0:0.
# b) If interfaces have been changed or added, but binary compatibility has
#    been preserved, change to C+1:0:A+1
# c) If the interface is the same as the previous version, change to C:R+1:A
liblur_so_c=3
liblur_so_r=3
liblur_so_a=0

# convert to sonames
liblur_so_version = '@0@.@1@.@2@'.format((liblur_so_c-liblur_so_a),
					 liblur_so_a, liblur_so_r)

# Compiler setup
cc = meson.get_compiler('c')
cflags = ['-Wno-unused-parameter',
	  '-fvisibility=hidden',
	  '-Wmissing-prototypes',
	  '-Wformat', # required by format-security
	  '-Werror=format-security',
	  '-Wstrict-prototypes']
add_project_arguments(cflags, language: 'c')

# Initialize config.h, to be added to in the various options below, config.h
# is generated at the end of this file
config_h = configuration_data()
config_h.set('_GNU_SOURCE', '1')
config_h.set_quoted('RATBAG_VERSION', meson.project_version())
config_h.set('RATBAGD_API_VERSION', ratbagd_api_version)
libratbag_data_dir = join_paths(get_option('prefix'),
				get_option('datadir'),
				'libratbag')
libratbag_data_dir_devel = join_paths(meson.source_root(), 'data', 'devices')
config_h.set_quoted('LIBRATBAG_DATA_DIR', libratbag_data_dir)

# Coverity breaks because it doesn't define _Float128 correctly, you'll end
# up with a bunch of messages in the form:
# "/usr/include/stdlib.h", line 133: error #20: identifier "_Float128" is
#           undefined
#   extern _Float128 strtof128 (const char *__restrict __nptr,
#          ^
# We don't use float128 ourselves, it gets pulled in from math.h or
# something, so let's just define it as uint128 and move on.
# Unfortunately we can't detect the coverity build at meson configure
# time, we only know it fails at runtime. So make this an option instead, to
# be removed when coverity fixes this again.
if get_option('coverity')
        config_h.set('_Float128', '__uint128_t')
        config_h.set('_Float32', 'int')
        config_h.set('_Float32x', 'int')
        config_h.set('_Float64', 'long')
        config_h.set('_Float64x', 'long')
endif

# dependencies
pkgconfig = import('pkgconfig')
dep_udev = dependency('libudev')
dep_libevdev = dependency('libevdev')
dep_glib = dependency('glib-2.0')
dep_json_glib = dependency('json-glib-1.0')
dep_lm = cc.find_library('m')
dep_unistring = cc.find_library('unistring')

if get_option('logind-provider') == 'elogind'
	dep_logind = dependency('libelogind', version : '>=227')
else
	dep_logind = dependency('libsystemd', version : '>=227')
endif

enable_systemd = get_option('systemd')
if enable_systemd
	dep_systemd = dependency('systemd')
endif

#### libutil.a ####
src_libutil = [
	'src/libratbag-util.c',
	'src/libratbag-util.h'
]

deps_libutil = [
	dep_udev,
]

lib_libutil = static_library('util',
	src_libutil,
	dependencies : deps_libutil
)
dep_libutil = declare_dependency(link_with: lib_libutil)

### libhidpp.a ####
src_libhidpp = [
	'src/hidpp-generic.h',
	'src/hidpp-generic.c',
	'src/hidpp10.h',
	'src/hidpp10.c',
	'src/hidpp20.h',
	'src/hidpp20.c',
	'src/usb-ids.h'
]

deps_libhidpp = [ dep_lm ]

lib_libhidpp = static_library('hidpp',
	src_libhidpp,
	dependencies : deps_libhidpp)
dep_libhidpp = declare_dependency(link_with: lib_libhidpp)

### liblur ####
#
# liblur is the library to handle logitech unifying receivers.
#
install_headers('src/liblur.h')

src_liblur = [
	'src/liblur.c',
	'src/liblur.h'
]

deps_liblur = [
	dep_libutil,
	dep_libhidpp,
]

lur_mapfile = 'src/liblur.sym'
lur_version_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), lur_mapfile)
lib_liblur = shared_library('lur',
	src_liblur,
	include_directories : include_directories('.'),
	dependencies : deps_liblur,
	version : liblur_so_version,
	link_args : lur_version_flag,
	link_depends : lur_mapfile,
	install : true,
)

dep_liblur = declare_dependency(link_with: lib_liblur)

pkgconfig.generate (
        filebase: 'liblur',
        name: 'Liblur',
        description: 'Logitech Unifying Receiver configuration library',
        version: meson.project_version(),
        libraries: lib_liblur
)

#### libratbag.so ####
#
# libratbag is an internal-only library and the bit that does the actual
# work talking to the mouse. it's used by ratbagd.
src_libratbag = [
	'src/libratbag-enums.h',
	'src/libratbag.h',
	'src/driver-etekcity.c',
	'src/driver-hidpp20.c',
	'src/driver-hidpp10.c',
	'src/driver-logitech-g300.c',
	'src/driver-logitech-g600.c',
	'src/driver-roccat.c',
	'src/driver-roccat-kone-pure.c',
	'src/driver-gskill.c',
	'src/driver-steelseries.c',
	'src/driver-sinowealth.c',
	'src/driver-test.c',
	'src/libratbag.c',
	'src/libratbag.h',
	'src/libratbag-data.c',
	'src/libratbag-data.h',
	'src/libratbag-hidraw.c',
	'src/libratbag-hidraw.h',
	'src/libratbag-private.h',
	'src/libratbag-test.c',
	'src/libratbag-test.h',
	'src/usb-ids.h'
]

deps_libratbag = [
	dep_udev,
	dep_libevdev,
	dep_glib,
	dep_json_glib,
	dep_libutil,
	dep_libhidpp,
]

lib_libratbag = static_library('ratbag',
	src_libratbag,
	include_directories : include_directories('.'),
	dependencies : deps_libratbag,
)

dep_libratbag = declare_dependency(
	link_with : lib_libratbag,
	dependencies : deps_libratbag
)

#### libshared.a ####
src_libshared = [
	'tools/shared.c',
	'tools/shared.h'
]

deps_libshared = [
	dep_udev,
	dep_libevdev,
]
lib_libshared = static_library('shared',
	src_libshared,
	dependencies : deps_libshared,
	include_directories : include_directories('src')
)
dep_libshared = declare_dependency(link_with: lib_libshared)

#### hidpp10-dump-page ####
src_hidpp10_dump_page = [ 'tools/hidpp10-dump-page.c' ]
executable('hidpp10-dump-page',
	src_hidpp10_dump_page,
	dependencies : [ dep_libhidpp ],
	include_directories : include_directories('src'),
	install : false,
)

#### hidpp20-dump-page ####
src_hidpp20_dump_page = [ 'tools/hidpp20-dump-page.c' ]
executable('hidpp20-dump-page',
	src_hidpp20_dump_page,
	dependencies : [ dep_libhidpp ],
	include_directories : include_directories('src'),
	install : false,
)

#### hidpp20-reset ####
src_hidpp20_reset = [ 'tools/hidpp20-reset.c' ]
executable('hidpp20-reset',
	src_hidpp20_reset,
	dependencies : [ dep_libhidpp ],
	include_directories : include_directories('src'),
	install : false,
)

#### lur-command ####
#
# A tool to access and manipulate logitech unifying receivers.
src_lur_command = [ 'tools/lur-command.c' ]
executable('lur-command',
	src_lur_command,
	dependencies : [ dep_libshared, dep_liblur ],
	include_directories : include_directories('src'),
	install : true,
)

man_config = configuration_data()

man_config.set('version', meson.project_version())

man_lur_command = configure_file (
	input: 'tools/lur-command.man',
	output: 'lur-command.1',
	configuration: man_config,
	install : true,
	install_dir : join_paths(get_option('mandir'), 'man1')
)

#### data files ####
# list ordered by git ls-files data/devices/*.device
data_files = files(
	'data/devices/etekcity-scroll-alpha.device',
	'data/devices/gskill-MX-780.device',
	'data/devices/logitech-M325.device',
	'data/devices/logitech-M570.device',
	'data/devices/logitech-M585-M590.device',
	'data/devices/logitech-M705.device',
	'data/devices/logitech-M720.device',
	'data/devices/logitech-MX-Ergo.device',
	'data/devices/logitech-MX-Master.device',
	'data/devices/logitech-MX-Master-2S.device',
	'data/devices/logitech-MX-Master-3.device',
	'data/devices/logitech-MX518.device',
	'data/devices/logitech-T650.device',
	'data/devices/logitech-Wireless-Touchpad.device',
	'data/devices/logitech-g-powerplay.device',
	'data/devices/logitech-g-pro-wireless.device',
	'data/devices/logitech-g-pro.device',
	'data/devices/logitech-g102-g203.device',
	'data/devices/logitech-g300.device',
	'data/devices/logitech-g302.device',
	'data/devices/logitech-g303.device',
	'data/devices/logitech-g305.device',
	'data/devices/logitech-g402.device',
	'data/devices/logitech-g403-hero.device',
	'data/devices/logitech-g403-wireless.device',
	'data/devices/logitech-g403.device',
	'data/devices/logitech-g5-2007.device',
	'data/devices/logitech-g5.device',
	'data/devices/logitech-g500.device',
	'data/devices/logitech-g500s.device',
	'data/devices/logitech-g502-hero-wireless.device',
	'data/devices/logitech-g502-hero.device',
	'data/devices/logitech-g502-proteus-core.device',
	'data/devices/logitech-g502-proteus-spectrum.device',
	'data/devices/logitech-g513.device',
	'data/devices/logitech-g600.device',
	'data/devices/logitech-g602.device',
	'data/devices/logitech-g603.device',
	'data/devices/logitech-g604.device',
	'data/devices/logitech-g7.device',
	'data/devices/logitech-g700-wireless.device',
	'data/devices/logitech-g700.device',
	'data/devices/logitech-g700s.device',
	'data/devices/logitech-g703-hero.device',
	'data/devices/logitech-g703.device',
	'data/devices/logitech-g9.device',
	'data/devices/logitech-g900.device',
	'data/devices/logitech-g903-hero.device',
	'data/devices/logitech-g903.device',
	'data/devices/logitech-g815.device',
	'data/devices/logitech-g910.device',
	'data/devices/logitech-g915.device',
	'data/devices/logitech-g935.device',
	'data/devices/logitech-g9x-Call-of-Duty-MW3-Edition.device',
	'data/devices/logitech-g9x-Original.device',
	'data/devices/logitech-Marathon-M705.device',
	'data/devices/logitech-MX-Anywhere2.device',
	'data/devices/logitech-MX-Anywhere2S.device',
	'data/devices/logitech-MX-Vertical.device',
	'data/devices/roccat-kone-pure.device',
	'data/devices/roccat-kone-xtd.device',
	'data/devices/steelseries-kinzu-v2.device',
	'data/devices/steelseries-kinzu-v3.device',
	'data/devices/steelseries-rival-310.device',
	'data/devices/steelseries-rival-600.device',
	'data/devices/steelseries-rival.device',
	'data/devices/steelseries-sensei-310.device',
	'data/devices/steelseries-sensei-raw.device',
	'data/devices/glorious-model-d.device',
	'data/devices/glorious-model-o.device',
)

install_data(data_files,
	     install_dir : join_paths(get_option('datadir'), 'libratbag'))

data_parse_test = find_program(join_paths(meson.source_root(), 'data/devices/data-parse-test.py'))
test('data-parse-test', data_parse_test, args:  data_files)

duplicate_test = find_program(join_paths(meson.source_root(), 'data/devices/duplicate-check.py'))
test('duplicate-test', duplicate_test, args : data_files)

reciever_id_test = find_program(join_paths(meson.source_root(), 'data/devices/receiver-check.py'))
test('receiver-id-test', reciever_id_test, args : data_files)

#### tests ####
enable_tests = get_option('tests')
if enable_tests
	dep_check = dependency('check', version: '>= 0.9.10')

	config_h.set('BUILD_TESTS', '1')

	test_context = executable('test-context',
				  ['test/test-context.c'],
				  dependencies : [ dep_libratbag, dep_check ],
				  include_directories : include_directories('src'),
				  install : false)
	test_device = executable('test-device',
				 ['test/test-device.c'],
				 dependencies : [ dep_libratbag, dep_check ],
				 include_directories : include_directories('src'),
				 install : false)
	test_util = executable('test-util',
				 ['test/test-util.c'],
				 dependencies : [ dep_libratbag, dep_check ],
				 include_directories : include_directories('src'),
				 install : false)
	test_iconv_helper = executable('test-iconv-helper',
				['test/test-iconv-helper.c'],
				dependencies : [ dep_libratbag,
						 dep_check,
						 dep_libutil],
				include_directories : include_directories('src'),
				install : false)
	test('test-context', test_context)
	test('test-device', test_device)
	test('test-util', test_util)
	test('test-iconv-helper', test_iconv_helper)

	valgrind = find_program('valgrind')
	valgrind_suppressions_file = join_paths(meson.source_root(), 'test', 'valgrind.suppressions')
	add_test_setup('valgrind',
		       exe_wrapper : [
			       valgrind,
			       '--leak-check=full',
			       '--quiet',
			       '--error-exitcode=3',
			       '--suppressions=' + valgrind_suppressions_file ],
		       timeout_multiplier: 5)
endif

#### ratbagd ####
#
# The main item of this repo, a DBus server that uses libratbag to talk to
# the mice. The DBus API is the public-facing API.
#
src_ratbagd = [
	'src/shared-macro.h',
	'src/shared-rbtree.h',
	'src/shared-rbtree.c',
	'ratbagd/ratbagd.h',
	'ratbagd/ratbagd.c',
	'ratbagd/ratbagd-led.c',
	'ratbagd/ratbagd-button.c',
	'ratbagd/ratbagd-device.c',
	'ratbagd/ratbagd-profile.c',
	'ratbagd/ratbagd-resolution.c',
	'ratbagd/ratbagd-test.c',
	'ratbagd/ratbagd-json.c',
	'ratbagd/ratbagd-json.h',
	'src/libratbag-util.h',
	'src/libratbag-util.c',
]

deps_ratbagd = [
	dep_udev,
	dep_logind,
	dep_libratbag,
	dep_unistring,
]

executable('ratbagd',
	   src_ratbagd,
	   dependencies : deps_ratbagd,
	   include_directories : include_directories('src'),
	   install : true,
)

install_man('ratbagd/ratbagd.8')

#### ratbagd_devel ####
#
# A development ratbagd server that owns a different name on the bus
# (org.freedesktop.ratbag_devel1). This server is used by ratbagdctl.devel.
#

config_ratbagd_devel = configuration_data()
dbus_devel_policy = configure_file(input : 'dbus/org.freedesktop.ratbag_devel1.conf.in',
				   output : 'org.freedesktop.ratbag_devel1.conf',
				   configuration : config_ratbagd_devel)

# This is a hack. We always install the devel policy file into
# /etc/dbus-1/system.d, independent of any prefixes we use otherwise.
# This should never be used outside of developer machines anyway, but
# installations on those may use different prefixes for building.
# This is not set in stone, suggest something better if you can.
dbussystemdir = join_paths('/', get_option('sysconfdir'), 'dbus-1', 'system.d')

executable('ratbagd.devel',
	   src_ratbagd,
	   dependencies : deps_ratbagd,
	   include_directories : include_directories('src'),
	   install : false,
	   c_args : ['-DRATBAG_DBUS_INTERFACE="ratbag_devel1"',
		     '-DDBUS_POLICY_SRC="@0@/@1@"'.format(meson.build_root(), dbus_devel_policy),
		     '-DDBUS_POLICY_DST="@0@/@1@"'.format(dbussystemdir, dbus_devel_policy),
		     '-DDISABLE_COREDUMP=1'],
)


#### unit file ####
if enable_systemd
	unitdir = get_option('systemd-unit-dir')
	if unitdir == ''
		libdir = get_option('libdir')
		default_unitdir = dep_systemd.get_pkgconfig_variable('systemdsystemunitdir')
		# Fedora uses lib64 but systemd is in lib. Hack around this so it
		# works out of the box.
		intended_unitdir = join_paths(get_option('prefix'), get_option('libdir'), 'systemd')
		if get_option('prefix') == '/usr' and intended_unitdir != default_unitdir
			message('''
			systemd unitdir libdir mismatch detected, changing unitdir to
				@0@
			or specify with
				mesonconf -Dsystemd-unit-dir=<path>

			See https://github.com/libratbag/libratbag/issues/188
			'''.format(default_unitdir))
			unitdir = default_unitdir
		else
			unitdir = intended_unitdir
		endif
	endif
endif

config_bindir = configuration_data()
config_bindir.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

if enable_systemd
	configure_file(input : 'ratbagd/ratbagd.service.in',
			output : 'ratbagd.service',
			configuration : config_bindir,
			install_dir : unitdir)
endif

dbusdir = get_option('dbus-root-dir')
if dbusdir == ''
	dbusdir = join_paths(get_option('prefix'), get_option('datadir'), 'dbus-1')
endif

configure_file(input : 'dbus/org.freedesktop.ratbag1.service.in',
	       output : 'org.freedesktop.ratbag1.service',
	       configuration : config_bindir,
	       install_dir : join_paths(dbusdir, 'system-services'))

dbusgroup = get_option('dbus-group')
if dbusgroup == ''
	# grant everybody access by default
	access = 'context="default"'
else
	# grant access to members of the specified group only
	access = 'group="@0@"'.format(dbusgroup)
endif

config_dbusaccess = configuration_data()
config_dbusaccess.set('access', access)

configure_file(input : 'dbus/org.freedesktop.ratbag1.conf.in',
	       output : 'org.freedesktop.ratbag1.conf',
	       configuration : config_dbusaccess,
	       install_dir : join_paths(dbusdir, 'system.d'))

#### tools ####

# This is a special idiomatic construct in meson to get a fast dependency that
# doesn't exist and isn't logged as "not found".
dep_python3 = dependency('', required : false)
if meson.version().version_compare('<0.48.0')
  python = import('python3')
  py3 = python.find_python()
  py_version = python.language_version()
else
  pymod = import('python')
  py3 = pymod.find_installation()
  if meson.version().version_compare('>= 0.53.0')
    dep_python3 = py3.dependency(embed : true)
  endif
  py_version = py3.language_version()
endif

# From python 3.8 we neeed python3-embed
embed = py_version.version_compare('>= 3.8') ? '-embed' : ''
if not dep_python3.found()
  dep_python3 = dependency('python-@0@@1@'.format(py_version, embed), required : false)
  if not dep_python3.found()
    dep_python3 = dependency('python3@0@'.format(embed))
  endif
endif

config_ratbagctl = configuration_data()
config_ratbagctl.set('RATBAGD_API_VERSION', ratbagd_api_version)
config_ratbagctl.set('version', meson.project_version())

ratbagctl_in = configure_file(input : 'tools/ratbagctl.in.in',
			      output : 'ratbagctl.in',
			      configuration: config_ratbagctl)

# ratbagctl is the commandline tool to interact with ratbagd over DBus.
custom_target('ratbagctl',
  output : 'ratbagctl',
  input : [ratbagctl_in, 'tools/ratbagd.py'],
  build_by_default : true,
  command : [py3, join_paths(meson.source_root(), 'tools', 'merge_ratbagd.py'),
				'@INPUT@',
				'--output', '@OUTPUT@'],
  install: true,
  install_dir: get_option('bindir'))

man_ratbagctl = configure_file (
	input: 'tools/ratbagctl.1',
	output: 'ratbagctl.1',
	configuration: man_config,
	install : true,
	install_dir : join_paths(get_option('mandir'), 'man1')
)

config_ratbagctl_devel = configuration_data()
config_ratbagctl_devel.set('MESON_BUILD_ROOT', meson.build_root())
config_ratbagctl_devel.set('LIBRATBAG_DATA_DIR', libratbag_data_dir_devel)
config_ratbagctl_devel.set('RATBAGD_API_VERSION', ratbagd_api_version)

configure_file(input : 'tools/toolbox.py',
	       output : 'toolbox.py',
	       configuration : config_ratbagctl_devel)
# ratbagctl.devel starts a custom ratbagd and interacts with that over DBus
configure_file(input : 'tools/ratbagctl.devel.in',
	       output : 'ratbagctl.devel',
	       configuration : config_ratbagctl_devel)
# ratbagctl.test starts a custom ratbagd and interacts with that over DBus
configure_file(input : 'tools/ratbagctl.test.in',
	       output : 'ratbagctl.test',
	       configuration : config_ratbagctl_devel)

env_test = environment()
env_test.set('LIBRATBAG_DATA_DIR', libratbag_data_dir_devel)
ratbagctl_test = find_program(join_paths(meson.build_root(), 'ratbagctl.test'))
test('ratbagctl-test', ratbagctl_test, args: ['-v'], env : env_test)

# ratbag-command uses Swig bindings to call libratbag directly
swig = find_program('swig')
swig_gen = generator(
    swig,
    output: ['@BASENAME@.c'],
    arguments: ['-python', '-py3', '-o', './@OUTPUT@',
                '-outdir', '.',
                '-I' + join_paths(meson.source_root(), 'src'),
                '-I' + join_paths(meson.source_root(), 'tools'),
                '@INPUT@'],
)

wrapper_deps = [
    dep_python3,
    dep_libratbag,
    dep_libshared,
]

i_source = join_paths(meson.source_root(), 'src', 'libratbag.i')
c_source = swig_gen.process(i_source)
shared_library(
   'libratbag',
   c_source,
   name_prefix: '_',
   c_args : ['-Wno-missing-prototypes',
             '-Wno-format-nonliteral',
             '-Wno-missing-field-initializers'],
   extra_files: [ i_source ] + src_libratbag ,
   dependencies: deps_libratbag + wrapper_deps,
   include_directories : include_directories('src', 'tools'),
   install: false,
)

i_source = join_paths(meson.source_root(), 'src', 'hidpp.i')
c_source = swig_gen.process(i_source)
shared_library(
   'hidpp',
   c_source,
   name_prefix: '_',
   c_args : ['-Wno-missing-prototypes',
             '-Wno-format-nonliteral',
             '-Wno-missing-field-initializers',
             '-Wno-strict-prototypes'],
   extra_files: [ i_source ] + src_libratbag ,
   dependencies: deps_libratbag + wrapper_deps,
   include_directories : include_directories('src', 'tools'),
   install: false,
)

# ratbagc is the layer that maps ratbagctl to the swig bindings
ratbagc_py_conf = configuration_data()
ratbagc_py_conf.set('LIBRATBAG_DATA_DIR', libratbag_data_dir_devel)
ratbagc_py = configure_file(input: 'tools/ratbagc.py.in',
			    output: 'ratbagc.py',
			    configuration: ratbagc_py_conf)

# ratbag-command is a commandline tool with the UI as ratbagctl but instead
# of connecting to ratbagd over DBus, it uses libratbag directly. This is a
# development tool for protocol debugging etc.
custom_target('ratbag-command',
  output : 'ratbag-command',
  input : [ratbagctl_in, ratbagc_py],
  build_by_default : true,
  command : [py3, join_paths(meson.source_root(), 'tools', 'merge_ratbagd.py'),
				'@INPUT@',
				'--output', '@OUTPUT@'],
  install: false)

#### output files ####
configure_file(output: 'config.h', install: false, configuration: config_h)

subdir('doc')
