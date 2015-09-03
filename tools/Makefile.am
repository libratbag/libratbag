noinst_PROGRAMS =
bin_PROGRAMS = ratbag-command
noinst_LTLIBRARIES = libshared.la

AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              -I$(top_builddir)/src  # for libratbag-version.h

libshared_la_SOURCES = \
		       shared.c \
		       shared.h
libshared_la_CFLAGS = $(LIBUDEV_LIBS) $(LIBEVDEV_CFLAGS)
libshared_la_LIBADD = $(LIBUDEV_CFLAGS) $(LIBEVDEV_LIBS)

ratbag_command_SOURCES = ratbag-command.c
ratbag_command_LDADD = ../src/libratbag.la libshared.la $(LIBUDEV_LIBS) $(LIBEVDEV_LIBS)
ratbag_command_LDFLAGS = -no-install
ratbag_command_CFLAGS = $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
