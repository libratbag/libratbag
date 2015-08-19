lib_LIBRARIES = \
	libhidpp10.a \
	libunifying.a

AM_CPPFLAGS=-I$(top_srcdir)/include/

libhidpp10_adir = \
	$(top_srcdir)/lib
libhidpp10_a_HEADERS = \
	$(top_srcdir)/include/hidpp10.h
libhidpp10_a_SOURCES = \
	libhidpp10_a_HEADERS \
	$(top_srcdir)/lib/hidpp10.c

libunifying_adir = \
	$(top_srcdir)/lib
libunifying_a_HEADERS = \
	$(top_srcdir)/include/unifying.h
libunifying_a_SOURCES = \
	libunifying_a_HEADERS \
	$(top_srcdir)/lib/unifying.c

# man page generation
if HAVE_DOCTOOLS
# actual man pages
man_pages_sources =
# shadow man pages
man_pages_shadows =

man_pages = $(man_pages_sources:.txt=.1) $(man_pages_shadows)

SUFFIXES = .1 .txt .xml

.xml.1:
	@$(XMLTO) man $<

.txt.xml:
	@$(ASCIIDOC) -b docbook -d manpage -o $@ $<

dist_man_MANS = $(man_pages)
CLEANFILES = $(dist_man_MANS)
MAINTAINERCLEANFILES = $(man_pages) *.xml
endif
