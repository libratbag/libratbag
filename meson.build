project('libratbag', 'c', 'cpp',
	version : '0.6.0',
	license : 'MIT/Expat',
	default_options : [ 'c_std=gnu99' ])

libratbag_version = meson.project_version().split('.')

# We use libtool-version numbers because it's easier to understand.
# Before making a release, the libratbag_so_* and liblur_so_*
# numbers should be modified. The components are of the form C:R:A.
# a) If binary compatibility has been broken (eg removed or changed interfaces)
#    change to C+1:0:0.
# b) If interfaces have been changed or added, but binary compatibility has
#    been preserved, change to C+1:0:A+1
# c) If the interface is the same as the previous version, change to C:R+1:A
libratbag_so_c=4
libratbag_so_r=0
libratbag_so_a=0
liblur_so_c=3
liblur_so_r=2
liblur_so_a=0

# convert to sonames
libratbag_so_version = '@0@.@1@.@2@'.format((libratbag_so_c - libratbag_so_a),
					    libratbag_so_a, libratbag_so_r)
liblur_so_version = '@0@.@1@.@2@'.format((liblur_so_c-liblur_so_a),
					 liblur_so_a, liblur_so_r)

# Compiler setup
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
cppflags = ['-Wall', '-Wextra', '-Wno-unused-parameter', '-g', '-fvisibility=hidden']
cflags = cppflags + ['-Wmissing-prototypes', '-Wstrict-prototypes']
add_project_arguments(cflags, language: 'c')
add_project_arguments(cppflags, language: 'cpp')

# Initialize config.h, to be added to in the various options below, config.h
# is generated at the end of this file
config_h = configuration_data()
config_h.set('_GNU_SOURCE', '1')

# dependencies
pkgconfig = import('pkgconfig')
dep_udev = dependency('libudev')
dep_libevdev = dependency('libevdev')
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
install_headers('src/libratbag.h')

src_libratbag = [
	'src/driver-etekcity.c',
	'src/driver-hidpp20.c',
	'src/driver-hidpp10.c',
	'src/driver-logitech-g300.c',
	'src/driver-roccat.c',
	'src/driver-gskill.c',
	'src/driver-test.c',
	'src/libratbag.c',
	'src/libratbag.h',
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
	dep_libutil,
	dep_libhidpp,
]

libratbag_version_h_config = configuration_data()
libratbag_version_h_config.set('LIBRATBAG_VERSION_MAJOR', libratbag_version[0])
libratbag_version_h_config.set('LIBRATBAG_VERSION_MINOR', libratbag_version[1])
libratbag_version_h_config.set('LIBRATBAG_VERSION_MICRO', libratbag_version[2])
libratbag_version_h_config.set('LIBRATBAG_VERSION', meson.project_version())

libratbag_version_h = configure_file(
	ratbag : 'src/libratbag-version.h.in',
	output : 'libratbag-version.h',
	configuration : libratbag_version_h_config,
	install: false,
)

mapfile = 'src/libratbag.sym'
version_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)
lib_libratbag = shared_library('ratbag',
	src_libratbag,
	include_directories : include_directories('.'),
	dependencies : deps_libratbag,
	version : libratbag_so_version,
	link_args : version_flag,
	link_depends : mapfile,
	install : true,
)

dep_libratbag = declare_dependency(
	link_with : lib_libratbag,
	dependencies : deps_libratbag
)

pkgconfig.generate(
        filebase: 'libratbag',
        name: 'Libratbag',
        description: 'Mouse configuration library',
        version: meson.project_version(),
        libraries: lib_libratbag
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
	install : true,
)

man_config = configuration_data()

man_config.set('version', meson.project_version())

man_ratbag_command = configure_file (
	input: 'tools/ratbag-command.man',
	output: 'ratbag-command.1',
	configuration: man_config,
	install : true,
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

#### lur-command ####
src_lur_command = [ 'tools/lur-command.c' ]
executable('lur-command',
	src_lur_command,
	dependencies : [ dep_libshared, dep_liblur ],
	include_directories : include_directories('src'),
	install : true,
)

#### hwdb ####
udev_dir = get_option('udev-dir')
if udev_dir == ''
        udev_dir = '@0@/lib/udev'.format(get_option('prefix'))
endif
udev_rules_dir = '@0@/rules.d'.format(udev_dir)
udev_hwdb_dir = '@0@/hwdb.d'.format(udev_dir)

install_data('hwdb/70-libratbag-mouse.rules',
	     install_dir : udev_rules_dir)
install_data('hwdb/70-libratbag-mouse.hwdb',
	     install_dir : udev_hwdb_dir)

#### svg files ####
svg_files = [
	'data/etekcity.svg',
	'data/logitech-g300.svg',
	'data/logitech-g303.svg',
	'data/logitech-g500s.svg',
	'data/logitech-g502.svg',
	'data/logitech-g700.svg',
	'data/logitech-g900.svg',
	'data/logitech-mx_master.svg',
	'data/roccat-kone-xtd.svg'
]
install_data(svg_files,
	     install_dir : '@0@/libratbag'.format(get_option('datadir')))

#### tests ####
enable_tests = get_option('enable-tests')
if enable_tests
	dep_check = dependency('check', version: '>= 0.9.10')

	config_h.set('BUILD_TESTS', '1')
	env_test = ['RATBAG_TEST=1']
	valgrind_args = [ '--leak-check=full',
			  '--quiet',
			  '--error-exitcode=3',
			  '--suppressions=@0@/test/valgrind.suppressions'.format(meson.current_source_dir()) ]

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
	test_iconv_helper = executable('test-iconv-helper',
				['test/test-iconv-helper.c'],
				dependencies : [ dep_libratbag,
						 dep_check,
						 dep_libutil],
				include_directories : include_directories('src'),
				install : false)
	test('test-context', test_context, env : env_test, valgrind_args : valgrind_args)
	test('test-device', test_device, env : env_test, valgrind_args : valgrind_args)
	test('test-iconv-helper', test_iconv_helper, env : env_test, valgrind_args : valgrind_args)

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

#### documentation ####
enable_doc = get_option('enable-documentation')

if enable_doc
	cmd_doxygen = find_program('doxygen')
	cmd_dot = find_program('dot')

	v = run_command(cmd_doxygen, ['--version']).stdout()
	if not v.version_compare('>=1.8.3')
		error('Doxygen $doxygen_version too old. ' +
		      'Doxygen 1.8.3+ required for documentation build. ' +
		      'Install required doxygen version or ' +
		      'disable the documentation using --disable-documentation')
	endif

	v = run_command(cmd_dot, ['-V']).stderr()
	v = v.split(' ')[4]
	if v.version_compare('<2.26.0')
		error('Graphviz dot $dot_version too old. ' +
		      'Graphviz 2.26+ required for documentation build. ' +
		      'Install required graphviz version or ' +
		      'disable the documentation using --disable-documentation')
	endif

	doc_config = configuration_data()
	doc_config.set('PACKAGE_NAME', meson.project_name())
	doc_config.set('PACKAGE_VERSION', meson.project_version())
	doc_config.set('top_srcdir', meson.source_root())

	doxyfile = configure_file(input : 'doc/libratbag.doxygen.in',
				  output : 'libratbag.doxygen',
				  configuration : doc_config,
				  install : false)
	src_doxygen = [ doxyfile,
			'src/libratbag.h',
			'README.md']
	custom_target('doxygen',
			input : src_doxygen,
			output : [ 'html' ],
			command : [ cmd_doxygen, doxyfile ],
			install : false,
			build_by_default : true)
endif

#### output files ####
configure_file(output: 'config.h', install: false, configuration: config_h)
