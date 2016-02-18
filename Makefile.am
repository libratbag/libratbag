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
# ratbagctl

bin_PROGRAMS += \
	ratbagctl

ratbagctl_SOURCES = \
	src/ratbagctl.c

ratbagctl_CFLAGS = \
	$(AM_CFLAGS) \
	$(LIBSYSTEMD_CFLAGS)

ratbagctl_LDADD = \
	libshared.la \
	$(LIBSYSTEMD_LIBS)

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
