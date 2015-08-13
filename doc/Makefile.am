if BUILD_DOCS

noinst_DATA = html/index.html

header_files = \
	$(top_srcdir)/src/libratbag.h \
	$(top_srcdir)/README.md

style_files = \
	      style/header.html \
	      style/footer.html \
	      style/customdoxygen.css \
	      style/bootstrap.css

html/index.html: libratbag.doxygen $(header_files) $(diagram_files) $(style_files)
	$(AM_V_GEN)(cat $<; \
          echo "INPUT = $(header_files)"; \
          ) | $(DOXYGEN) -

clean-local:
	$(AM_V_at)rm -rf html

doc_src= $(shell find html -type f -printf "html/%P\n" 2>/dev/null)
EXTRA_DIST = $(builddir)/html/index.html \
	     $(doc_src) \
	     $(diagram_files) \
	     $(header_files) \
	     $(style_files)
endif

# make sure doc was built before running dist
dist-hook:
	@test -f $(distdir)/html/index.html || (\
		echo "******************************************************" && \
		echo "Couldn't find documentation files, refusing make dist." && \
		echo "Install doxygen to build documentation for tarball." && \
		echo "******************************************************" && \
		test )
