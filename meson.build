project('ratbagd', 'c',
	version : '0.2',
	license : 'MIT/Expat',
	meson_version : '>= 0.40.0',
	default_options : [ 'c_std=gnu99', 'warning_level=2' ])

ratbagd_version = meson.project_version().split('.')

# Compiler setup
cc = meson.get_compiler('c')
cflags = [
	'-pipe',
	'-Wno-inline',
	'-Wundef',
	'-Wformat=2',
	'-Wformat-security',
	'-Wformat-nonliteral',
	'-Wlogical-op',
	'-Wsign-compare',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wdeclaration-after-statement',
	'-Wfloat-equal',
	'-Wsuggest-attribute=noreturn',
	'-Wmissing-prototypes',
	'-Wstrict-prototypes',
	'-Wredundant-decls',
	'-Wmissing-declarations',
	'-Wmissing-noreturn',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Wwrite-strings',
	'-Wno-long-long',
	'-Wno-overlength-strings',
	'-Wno-unused-parameter',
	'-Wno-missing-field-initializers',
	'-Wno-unused-result',
	'-Werror=overflow',
	'-Wdate-time',
	'-Wnested-externs',
	'-ffast-math',
	'-fno-common',
	'-fdiagnostics-show-option',
	'-fno-strict-aliasing',
	'-fvisibility=hidden',
	'-ffunction-sections',
	'-fdata-sections',
	'-fstack-protector',
	'-fstack-protector-strong',
	'-fPIE',
	'--param=ssp-buffer-size=4'
]
add_project_arguments(cflags, language: 'c')
add_project_arguments('-D_GNU_SOURCE', language : 'c')

# Initialize config.h, to be added to in the various options below, config.h
# is generated at the end of this file
config_h = configuration_data()

# dependencies
pkgconfig = import('pkgconfig')
dep_udev = dependency('libudev')
dep_systemd = dependency('libsystemd', version : '>=227')
dep_libratbag = dependency('libratbag', version : '>= 0.6')
dep_lm = cc.find_library('m')

libratbag_data_dir = dep_libratbag.get_pkgconfig_variable('pkgdatadir')
config_h.set_quoted('LIBRATBAG_DATA_DIR', libratbag_data_dir)

#### libshared.a ####
src_libshared = [
	'src/shared-macro.h',
	'src/shared-rbtree.h',
	'src/shared-rbtree.c',
]

deps_libshared = [
	dep_lm,
]

lib_libshared = static_library('shared',
	src_libshared,
	dependencies : deps_libshared
)
dep_libshared = declare_dependency(link_with: lib_libshared)

#### ratbagd ####
src_ratbagd = [
	'src/ratbagd.h',
	'src/ratbagd.c',
	'src/ratbagd-button.c',
	'src/ratbagd-device.c',
	'src/ratbagd-profile.c',
	'src/ratbagd-resolution.c'
]

deps_ratbagd = [
	dep_udev,
	dep_systemd,
	dep_libratbag,
	dep_libshared,
]

executable('ratbagd',
	   src_ratbagd,
	   dependencies : deps_ratbagd,
	   include_directories : include_directories('src'),
	   install : true,
)

install_man('doc/ratbagd.8')

#### unit file ####
unitdir = get_option('systemd-unit-dir')
if unitdir == ''
	unitdir = get_option('libdir') + '/systemd/system'
endif

config_bindir = configuration_data()
config_bindir.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

configure_file(input : 'ratbagd.service.in',
	       output : 'ratbagd.service',
	       configuration : config_bindir,
	       install_dir : unitdir)

dbusdir = get_option('dbus-root-dir')
if dbusdir == ''
	dbusdir = get_option('datadir') + '/dbus-1'
endif

configure_file(input : 'dbus/org.freedesktop.ratbag1.service.in',
	       output : 'org.freedesktop.ratbag1.service',
	       configuration : config_bindir,
	       install_dir : dbusdir + '/system-services')
install_data('dbus/org.freedesktop.ratbag1.conf',
	     configuration : config_bindir,
	     install_dir : dbusdir + '/system.d')

#### python bindings ####

py3_mod = import('python3')
py3 = py3_mod.find_python()
py3_dep = dependency('python3', required : true)

pysrc_ratbagd =  [ 'python/ratbagd/__init__.py' ]

py3_purelib = py3_mod.sysconfig_path('purelib')

install_data(pysrc_ratbagd,
	     install_dir : py3_purelib + '/ratbagd')

config_ratbagctl = configuration_data()
config_ratbagctl.set('version', meson.project_version())
configure_file(input : 'python/ratbagctl.in',
	       output : 'ratbagctl',
	       configuration : config_ratbagctl,
	       install_dir : get_option('bindir'))

install_man('doc/ratbagctl.1')

#### output files ####
configure_file(output: 'config.h', install: false, configuration: config_h)
