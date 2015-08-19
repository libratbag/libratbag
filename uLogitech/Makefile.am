SUBDIRS = \
	lib \
	src

pkgconfigdir = $(libdir)/pkgconfig

AM_CPPFLAGS = $(top_srcdir)/include/

.PHONY: ChangeLog INSTALL
INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	@if test -d ".git"; then \
	    cmd=git; \
	else \
	    cmd="echo could not generate change"; \
	fi; \
	$${cmd} log > ChangeLog;

dist-hook: ChangeLog INSTALL
