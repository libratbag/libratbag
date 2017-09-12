project('libratbag', 'c', 'cpp',
	version : '0.9.900',
	license : 'MIT/Expat',
	default_options : [ 'c_std=gnu99', 'warning_level=2' ])

libratbag_version = meson.project_version().split('.')

git = find_program('git')
r = run_command('tools/ratbag-git-sha.sh')
ratbagd_sha = r.stdout().strip()

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
cpp = meson.get_compiler('cpp')
cppflags = ['-Wno-unused-parameter', '-fvisibility=hidden']
cflags = cppflags + ['-Wmissing-prototypes', '-Wstrict-prototypes']
add_project_arguments(cflags, language: 'c')
add_project_arguments(cppflags, language: 'cpp')

# Initialize config.h, to be added to in the various options below, config.h
# is generated at the end of this file
config_h = configuration_data()
config_h.set('_GNU_SOURCE', '1')
config_h.set_quoted('FALLBACK_SVG_NAME', 'fallback.svg')
libratbag_data_dir = join_paths(get_option('prefix'),
				get_option('datadir'),
				'libratbag')
libratbag_data_dir_devel = join_paths(meson.source_root(), 'data', 'devices')
config_h.set_quoted('LIBRATBAG_DATA_DIR', libratbag_data_dir)

# dependencies
pkgconfig = import('pkgconfig')
dep_udev = dependency('libudev')
dep_libevdev = dependency('libevdev')
dep_libsystemd = dependency('libsystemd', version : '>=227')
dep_systemd = dependency('systemd')
dep_glib = dependency('glib-2.0')
dep_lm = cc.find_library('m')

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

### liblur.a ####
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
src_libratbag = [
	'src/libratbag.h',
	'src/driver-etekcity.c',
	'src/driver-hidpp20.c',
	'src/driver-hidpp10.c',
	'src/driver-logitech-g300.c',
	'src/driver-roccat.c',
	'src/driver-gskill.c',
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
	dep_libutil,
	dep_libhidpp,
]

libratbag_version_h_config = configuration_data()
libratbag_version_h_config.set_quoted('LIBRATBAG_VERSION', meson.project_version())

libratbag_version_h = configure_file(
	output : 'libratbag-version.h',
	configuration : libratbag_version_h_config,
	install: false,
)

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

#### ratbag-command ####
src_ratbag_command = [ 'tools/ratbag-command.c' ]
deps_ratbag_command = [
	dep_libshared,
	dep_libratbag,
	dep_libevdev
]

executable('ratbag-command',
	src_ratbag_command,
	dependencies : deps_ratbag_command,
	include_directories : include_directories('src'),
	install : false,
)

man_config = configuration_data()

man_config.set('version', meson.project_version())

man_ratbag_command = configure_file (
	input: 'tools/ratbag-command.man',
	output: 'ratbag-command.1',
	configuration: man_config,
	install : false,
	install_dir : join_paths(get_option('mandir'), 'man1')
)

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
src_lur_command = [ 'tools/lur-command.c' ]
executable('lur-command',
	src_lur_command,
	dependencies : [ dep_libshared, dep_liblur ],
	include_directories : include_directories('src'),
	install : true,
)

man_lur_command = configure_file (
	input: 'tools/lur-command.man',
	output: 'lur-command.1',
	configuration: man_config,
	install : true,
	install_dir : join_paths(get_option('mandir'), 'man1')
)

#### data files ####
data_files = files(
	'data/devices/etekcity-fixme.device',
	'data/devices/gskill-MX-780.device',
	'data/devices/logitech-g300.device',
	'data/devices/logitech-g303.device',
	'data/devices/logitech-g403.device',
	'data/devices/logitech-g500.device',
	'data/devices/logitech-g500s.device',
	'data/devices/logitech-g502-Proteus-Core-over-USB.device',
	'data/devices/logitech-g502-Proteus-Spectrum-over-USB.device',
	'data/devices/logitech-g5-2007.device',
	'data/devices/logitech-g5.device',
	'data/devices/logitech-g602.device',
	'data/devices/logitech-g700.device',
	'data/devices/logitech-g700s.device',
	'data/devices/logitech-g700s-wireless.device',
	'data/devices/logitech-g700-wireless.device',
	'data/devices/logitech-g7.device',
	'data/devices/logitech-g900.device',
	'data/devices/logitech-g900-wireless.device',
	'data/devices/logitech-g9.device',
	'data/devices/logitech-g9x-[Call-of-Duty-MW3-Edition].device',
	'data/devices/logitech-g9x-[Original].device',
	'data/devices/logitech-M325.device',
	'data/devices/logitech-M570.device',
	'data/devices/logitech-M705.device',
	'data/devices/logitech-MX-Master.device',
	'data/devices/logitech-T650.device',
	'data/devices/logitech-Wireless-Touchpad.device',
	'data/devices/roccat-kone-xtd.device',
)

install_data(data_files,
	     install_dir : join_paths(get_option('datadir'), 'libratbag'))

data_parse_test = find_program(join_paths(meson.source_root(), 'data/devices/data-parse-test.py'))
test('data-parse-test', data_parse_test,
     args : [ '--svg-dir', join_paths(meson.source_root(), 'data/default'),
	      '--svg-dir', join_paths(meson.source_root(), 'data/gnome') ] + data_files)

#### svg files ####
default_svg_files = [
	'data/default/etekcity.svg',
	'data/default/logitech-g300.svg',
	'data/default/logitech-g303.svg',
	'data/default/logitech-g500s.svg',
	'data/default/logitech-g502.svg',
	'data/default/logitech-g700.svg',
	'data/default/logitech-g900.svg',
	'data/default/logitech-mx_master.svg',
	'data/default/roccat-kone-xtd.svg'
]
install_data(default_svg_files,
	     install_dir : join_paths(get_option('datadir'), 'libratbag', 'default'))

gnome_svg_files = files(
	'data/gnome/fallback.svg',
	'data/gnome/logitech-g303.svg',
	'data/gnome/logitech-g403.svg',
	'data/gnome/logitech-g500.svg',
	'data/gnome/logitech-g500s.svg',
	'data/gnome/logitech-g502.svg',
	'data/gnome/logitech-g700.svg',
	'data/gnome/logitech-g900.svg',
	'data/gnome/roccat-kone-xtd.svg',
)
install_data(gnome_svg_files,
	     install_dir : join_paths(get_option('datadir'), 'libratbag', 'gnome'))

test_svg_files = find_program('data/gnome/check-svg.py')
test('check-svg', test_svg_files, args : gnome_svg_files)

#### tests ####
env_test = environment()
env_test.set('LIBRATBAG_DATA_DIR', libratbag_data_dir_devel)

enable_tests = get_option('enable-tests')
if enable_tests
	dep_check = dependency('check', version: '>= 0.9.10')

	config_h.set('BUILD_TESTS', '1')
	env_test.set('RATBAG_TEST', '1')

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
	test('test-context', test_context, env : env_test)
	test('test-device', test_device, env : env_test)
	test('test-util', test_util, env : env_test)
	test('test-iconv-helper', test_iconv_helper, env : env_test)

	valgrind = find_program('valgrind')
	valgrind_suppressions_file = join_paths(meson.source_root(), 'test', 'valgrind.suppressions')
	add_test_setup('valgrind',
		       exe_wrapper : [
			       valgrind,
			       '--leak-check=full',
			       '--quiet',
			       '--error-exitcode=3',
			       '--suppressions=' + valgrind_suppressions_file ],
		       env : env_test,
		       timeout_multiplier: 5)

	executable('test-build-cxx',
		   ['test/build-cxx.cc'],
		   dependencies : [ dep_libratbag ],
		   include_directories : include_directories('src'),
		   cpp_args : ['-Wall', '-Wextra', '-Wno-unused-parameter'],
		   install : false)
	executable('test-build-linker',
		   ['test/build-pedantic.c'],
		   dependencies : [ dep_libratbag ],
		   include_directories : include_directories('src'),
		   install : false)
	executable('test-build-gnuc90',
		   ['test/build-pedantic.c'],
		   dependencies : [ dep_libratbag ],
		   include_directories : include_directories('src'),
		   c_args : ['-std=gnu90', '-Werror'],
		   install : false)
	executable('test-build-pedantic',
		   ['test/build-pedantic.c'],
		   dependencies : [ dep_libratbag ],
		   include_directories : include_directories('src'),
		   c_args : ['-std=c99', '-pedantic', '-Werror'],
		   install : false)
endif

test_symbols_leak = find_program('test/symbols-leak-test')
test('symbols-leak-test',
     test_symbols_leak,
     args : [ meson.current_source_dir() ])

#### ratbagd ####
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
	'src/libratbag-util.h',
	'src/libratbag-util.c',
]

deps_ratbagd = [
	dep_udev,
	dep_libsystemd,
	dep_libratbag,
]

executable('ratbagd',
	   src_ratbagd,
	   dependencies : deps_ratbagd,
	   include_directories : include_directories('src'),
	   install : true,
)

install_man('ratbagd/ratbagd.8')

#### ratbagd_devel ####

executable('ratbagd.devel',
	   src_ratbagd,
	   dependencies : deps_ratbagd,
	   include_directories : include_directories('src'),
	   install : false,
	   c_args : '-DRATBAG_DBUS_INTERFACE="ratbag_devel1_@0@"'.format(ratbagd_sha),
)

config_ratbagd_devel = configuration_data()
config_ratbagd_devel.set('ratbagd_sha', ratbagd_sha)
configure_file(input : 'dbus/org.freedesktop.ratbag_devel1.conf.in',
	       output : 'org.freedesktop.ratbag_devel1.conf',
	       configuration : config_ratbagd_devel)

#### unit file ####
unitdir = get_option('systemd-unit-dir')
if unitdir == ''
	libdir = get_option('libdir')
	default_unitdir = dep_systemd.get_pkgconfig_variable('systemdsystemunitdir')
	# Fedora uses lib64 but systemd is in lib. Hack around this so it
	# works out of the box.
	intended_unitdir = join_paths(get_option('prefix'), get_option('libdir'), 'systemd')
	if get_option('prefix') == '/usr' and intended_unitdir != default_unitdir
		message('
		systemd unitdir libdir mismatch detected, changing unitdir to
			@0@
		or specify with
			mesonconf -Dsystemd-unit-dir=<path>

		See https://github.com/libratbag/libratbag/issues/188
		'.format(default_unitdir))
		unitdir = default_unitdir
	else
		unitdir = intended_unitdir
	endif
endif

config_bindir = configuration_data()
config_bindir.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

configure_file(input : 'ratbagd/ratbagd.service.in',
	       output : 'ratbagd.service',
	       configuration : config_bindir,
	       install_dir : unitdir)

dbusdir = get_option('dbus-root-dir')
if dbusdir == ''
	dbusdir = join_paths(get_option('datadir'), 'dbus-1')
endif

configure_file(input : 'dbus/org.freedesktop.ratbag1.service.in',
	       output : 'org.freedesktop.ratbag1.service',
	       configuration : config_bindir,
	       install_dir : join_paths(dbusdir, 'system-services'))
install_data('dbus/org.freedesktop.ratbag1.conf',
	     install_dir : join_paths(dbusdir, 'system.d'))

#### tools ####

python = import('python3')
py3 = python.find_python()

custom_target('ratbagctl',
  output : 'ratbagctl',
  input : ['tools/ratbagctl.in', 'tools/ratbagd.py'],
  build_by_default : true,
  command : [py3, join_paths(meson.source_root(), 'tools', 'merge_ratbagd.py'),
				'@INPUT@',
				'--output', '@OUTPUT@',
				'--version', meson.project_version()],
  install: true,
  install_dir: get_option('bindir'))
install_man('tools/ratbagctl.1')

config_ratbagctl_devel = configuration_data()
config_ratbagctl_devel.set('ratbagd_sha', ratbagd_sha)
config_ratbagctl_devel.set('MESON_BUILD_ROOT', meson.build_root())
config_ratbagctl_devel.set('LIBRATBAG_DATA_DIR', libratbag_data_dir_devel)
configure_file(input : 'tools/ratbagctl.devel.in',
	       output : 'ratbagctl.devel',
	       configuration : config_ratbagctl_devel)
configure_file(input : 'tools/ratbagctl.test.in',
	       output : 'ratbagctl.test',
	       configuration : config_ratbagctl_devel)
ratbagctl_test = find_program(join_paths(meson.build_root(), 'ratbagctl.test'))
test('ratbagctl-test', ratbagctl_test, args: ['-v'], env : env_test)

configure_file(input : 'tools/toolbox.py',
	       output : 'toolbox.py',
	       configuration : config_ratbagctl_devel)

#### output files ####
configure_file(output: 'config.h', install: false, configuration: config_h)

