lib_LTLIBRARIES = libratbag.la

include_HEADERS =			\
	libratbag.h

libratbag_la_SOURCES =			\
	libratbag.c			\
	libratbag.h			\
	libratbag-private.h

libratbag_la_LIBADD = $(LIBEVDEV_LIBS)

libratbag_la_CFLAGS = -I$(top_srcdir)/include \
		      $(LIBEVDEV_CFLAGS)	\
		      $(GCC_CFLAGS)
EXTRA_libratbag_la_DEPENDENCIES = $(srcdir)/libratbag.sym

libratbag_la_LDFLAGS = -version-info $(LIBRATBAG_LT_VERSION) -shared \
		      -Wl,--version-script=$(srcdir)/libratbag.sym

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libratbag.pc

AM_CFLAGS = $(GCC_CFLAGS)

DISTCLEANFILES = libratbag-version.h
EXTRA_DIST = libratbag-version.h.in libratbag.sym
