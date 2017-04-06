# ------------------------------------------------------------------------------
# main

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

SUBDIRS = .

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# Keep the test-suite.log
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES =
EXTRA_DIST =
BUILT_SOURCES =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =
bin_PROGRAMS =
noinst_PROGRAMS =
dist_man1_MANS =
dist_man8_MANS =

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# libshared

noinst_LTLIBRARIES += \
	libshared.la

libshared_la_SOURCES = \
	src/shared-macro.h \
	src/shared-rbtree.h \
	src/shared-rbtree.c

libshared_la_CFLAGS = \
	$(AM_CFLAGS)

libshared_la_LIBADD = \
	-lm

# ------------------------------------------------------------------------------
# ratbagd

bin_PROGRAMS += \
	ratbagd

ratbagd_SOURCES = \
	src/ratbagd.h \
	src/ratbagd.c \
	src/ratbagd-button.c \
	src/ratbagd-device.c \
	src/ratbagd-profile.c \
	src/ratbagd-resolution.c

ratbagd_CFLAGS = \
	$(AM_CFLAGS) \
	$(LIBUDEV_CFLAGS) \
	$(LIBSYSTEMD_CFLAGS) \
	$(LIBRATBAG_CFLAGS)

ratbagd_LDADD = \
	libshared.la \
	$(LIBUDEV_LIBS) \
	$(LIBSYSTEMD_LIBS) \
	$(LIBRATBAG_LIBS)

dist_man8_MANS += doc/ratbagd.8

# ------------------------------------------------------------------------------
# DBus and systemd configuration files

unitdir = $(SYSTEMD_UNIT_DIR)
unit_DATA = ratbagd.service

$(unit_DATA): $(unit_DATA).in
	$(AM_V_GEN)$(SED) -e 's|@bindir@|$(bindir)|g' < $< > $@

dbusservicedir = $(DBUS_SYSTEM_SERVICES_DIR)
dbusservice_DATA = dbus/org.freedesktop.ratbag1.service

$(dbusservice_DATA): $(dbusservice_DATA).in
	$(AM_V_GEN)$(MKDIR_P) dbus
	$(AM_V_GEN)$(SED) -e 's|@bindir@|$(bindir)|g' < $< > $@

dbusconfigdir = $(DBUS_SYSTEM_CONFIG_DIR)
dbusconfig_DATA = dbus/org.freedesktop.ratbag1.conf

EXTRA_DIST += \
	      ratbagd.service.in \
	      dbus/org.freedesktop.ratbag1.service.in \
	      dbus/org.freedesktop.ratbag1.conf
BUILT_SOURCES += ratbagd.service dbus/org.freedesktop.ratbag1.service

# ------------------------------------------------------------------------------
# python module
dist_pkgpython_DATA = \
		      python/ratbagd/__init__.py

bin_SCRIPTS = \
	      python/ratbagctl

python/ratbagctl: python/ratbagctl.py
	$(AM_V_GEN)$(MKDIR_P) python
	$(AM_V_GEN)$(SED) -e "s/@version@/$(PACKAGE_VERSION)/" < $^ > $@

EXTRA_DIST += python/ratbagctl.py
BUILT_SOURCES += python/ratbagctl

dist_man1_MANS += doc/ratbagctl.1
