noinst_PROGRAMS = hidpp10-dump-page hidpp20-dump-page
bin_PROGRAMS = ratbag-command lur-command
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
ratbag_command_CFLAGS = $(LIBUDEV_CFLAGS) $(LIBEVDEV_CFLAGS)
dist_man1_MANS = ratbag-command.man

hidpp10_dump_page_SOURCES = hidpp10-dump-page.c
hidpp10_dump_page_LDADD = ../src/libhidpp.la
hidpp10_dump_page_LDFLAGS = -no-install
hidpp10_dump_page_CFLAGS = -I$(top_srcdir)/src

hidpp20_dump_page_SOURCES = hidpp20-dump-page.c
hidpp20_dump_page_LDADD = ../src/libhidpp.la
hidpp20_dump_page_LDFLAGS = -no-install
hidpp20_dump_page_CFLAGS = -I$(top_srcdir)/src

lur_command_SOURCeS = lur-command.c
lur_command_LDADD = ../src/liblur.la
lur_command_CFLAGS = -I$(top_srcdir)/src
