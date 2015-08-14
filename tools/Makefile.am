noinst_PROGRAMS = ratbag-command
bin_PROGRAMS = 
noinst_LTLIBRARIES =

AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              -I$(top_builddir)/src  # for libratbag-version.h

ratbag_command_SOURCES = ratbag-command.c
ratbag_command_LDADD = ../src/libratbag.la
ratbag_command_LDFLAGS = -no-install
ratbag_command_CFLAGS =
