lib_LTLIBRARIES = libratbag.la \
		  liblur.la
noinst_LTLIBRARIES = libhidpp.la \
		     libutil.la

include_HEADERS =			\
	libratbag.h			\
	liblur.h

libratbag_la_SOURCES =			\
	driver-etekcity.c		\
	driver-hidpp20.c		\
	driver-hidpp10.c		\
	driver-roccat.c			\
	driver-gskill.c			\
	driver-test.c			\
	libratbag.c			\
	libratbag.h			\
	libratbag-hidraw.c		\
	libratbag-hidraw.h		\
	libratbag-private.h		\
	libratbag-test.c		\
	libratbag-test.h		\
	usb-ids.h

libratbag_la_LIBADD = 			\
	libhidpp.la			\
	libutil.la			\
	$(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)

libratbag_la_CFLAGS = -I$(top_srcdir)/include \
		      $(LIBUDEV_CFLAGS)		\
		      $(LIBEVDEV_CFLAGS)	\
		      $(GCC_CFLAGS)
EXTRA_libratbag_la_DEPENDENCIES = $(srcdir)/libratbag.sym

libratbag_la_LDFLAGS = -version-info $(LIBRATBAG_LT_VERSION) -shared \
		      -Wl,--version-script=$(srcdir)/libratbag.sym

liblur_la_SOURCES = 			\
	liblur.c			\
	liblur.h
liblur_la_LIBADD =			\
	libhidpp.la			\
	libutil.la
liblur_la_CFLAGS = 			\
	-I$(top_srcdir)/include 	\
	$(GCC_CFLAGS)
EXTRA_liblur_la_DEPENDENCIES = $(srcdir)/liblur.sym

liblur_la_LDFLAGS = -version-info $(LIBLUR_LT_VERSION) -shared \
		      -Wl,--version-script=$(srcdir)/liblur.sym

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libratbag.pc \
		 liblur.pc

AM_CFLAGS = $(GCC_CFLAGS)

DISTCLEANFILES = libratbag-version.h
EXTRA_DIST = libratbag-version.h.in libratbag.sym liblur.sym

# The hidpp libraries are static libraries only for in-tree tools. No API
# stability is guaranteed here.
libhidpp_la_SOURCES = \
			hidpp-generic.h	\
			hidpp-generic.c	\
			hidpp10.h	\
			hidpp10.c	\
			hidpp20.h	\
			hidpp20.c 	\
			usb-ids.h
libhidpp_la_LDFLAGS = -lm
libutil_la_SOURCES = \
		     libratbag-util.c \
		     libratbag-util.h
libutil_la_LIBADD = $(LIBUDEV_LIBS)
