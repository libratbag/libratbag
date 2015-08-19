noinst_PROGRAMS =

bin_PROGRAMS = \
	m705_button6_toggler \
	pairing_tool \
	unpairing_tool \
	list_unifying_devices

LDADD = $(top_srcdir)/lib/libhidpp10.a $(top_srcdir)/lib/libunifying.a $(AM_LDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir)/include/

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
