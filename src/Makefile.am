lib_LTLIBRARIES = libratbag.la

include_HEADERS =			\
	libratbag.h

libratbag_la_SOURCES =			\
	driver-etekcity.c		\
	driver-hidpp20.c		\
	driver-hidpp10.c		\
	driver-test.c			\
	hidpp-generic.h			\
	hidpp-generic.c			\
	hidpp10.c			\
	hidpp10.h			\
	hidpp20.c			\
	hidpp20.h			\
	libratbag.c			\
	libratbag.h			\
	libratbag-hidraw.c		\
	libratbag-hidraw.h		\
	libratbag-util.c		\
	libratbag-private.h		\
	libratbag-util.h		\
	libratbag-test.c		\
	libratbag-test.h

libratbag_la_LIBADD = $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)

libratbag_la_CFLAGS = -I$(top_srcdir)/include \
		      $(LIBUDEV_CFLAGS)		\
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
