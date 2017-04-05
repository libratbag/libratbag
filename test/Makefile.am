noinst_SCRIPTS = symbols-leak-test
TESTS = symbols-leak-test

AM_TESTS_ENVIRONMENT=RATBAG_TEST=1; export RATBAG_TEST; TOP_SRCDIR="$(top_srcdir)"; export TOP_SRCDIR;

if BUILD_TESTS
AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src \
              $(CHECK_CFLAGS) \
              $(LIBEVDEV_CFLAGS)

AM_CFLAGS = $(GCC_CFLAGS)
AM_CXXFLAGS = $(GCC_CXXFLAGS)

TEST_LIBS = $(CHECK_LIBS) $(LIBEVDEV_LIBS) $(top_builddir)/src/libratbag.la

run_tests = \
	test-context \
	test-device \
	test-iconv-helper

build_tests = \
	test-build-cxx \
	test-build-linker \
	test-build-pedantic-c99 \
	test-build-std-gnuc90

noinst_PROGRAMS = $(build_tests) $(run_tests)
TESTS += $(run_tests)

test_context_SOURCES = test-context.c
test_context_LDADD = $(TEST_LIBS)
test_context_LDFLAGS = -no-install

test_device_SOURCES = test-device.c
test_device_LDADD = $(TEST_LIBS)
test_device_LDFLAGS = -no-install

test_iconv_helper_SOURCES = test-iconv-helper.c
test_iconv_helper_LDADD = $(TEST_LIBS) $(top_builddir)/src/libutil.la
test_iconv_helper_LDFLAGS = -no-install

# build-test only
test_build_pedantic_c99_SOURCES = build-pedantic.c
test_build_pedantic_c99_CFLAGS = -std=c99 -pedantic -Werror

test_build_std_gnuc90_SOURCES = build-pedantic.c
test_build_std_gnuc90_CFLAGS = -std=gnu90 -Werror

# test for linking with the minimal linker flags
test_build_linker_SOURCES = build-pedantic.c
test_build_linker_CFLAGS = -I$(top_srcdir)/src
test_build_linker_LDADD = $(top_builddir)/src/libratbag.la

# test including from C++
test_build_cxx_SOURCES = build-cxx.cc
test_build_cxx_CXXFLAGS = -Wall -Wextra -Wno-unused-parameter $(AM_CXXFLAGS)


if HAVE_VALGRIND
VALGRIND_FLAGS=--leak-check=full \
	       --quiet \
	       --error-exitcode=3 \
	       --suppressions=$(srcdir)/valgrind.suppressions

valgrind:
	$(MAKE) check-TESTS LOG_COMPILER="$(VALGRIND)" LOG_FLAGS="$(VALGRIND_FLAGS)" CK_FORK=no USING_VALGRIND=yes

check: valgrind

endif
endif
EXTRA_DIST=valgrind.suppressions symbols-leak-test
