AC_PREREQ([2.64])

m4_define([libratbag_major_version], [0])
m4_define([libratbag_minor_version], [1])
m4_define([libratbag_micro_version], [0])
m4_define([libratbag_version],
          [libratbag_major_version.libratbag_minor_version.libratbag_micro_version])

AC_INIT([libratbag],
        [libratbag_version],
        [],
        [libratbag],
        [])

AC_SUBST([LIBRATBAG_VERSION_MAJOR], [libratbag_major_version])
AC_SUBST([LIBRATBAG_VERSION_MINOR], [libratbag_minor_version])
AC_SUBST([LIBRATBAG_VERSION_MICRO], [libratbag_micro_version])
AC_SUBST([LIBRATBAG_VERSION], [libratbag_version])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

# Before making a release, the LIBRATBAG_LT_VERSION string should be
# modified.
# The string is of the form C:R:A.
# a) If binary compatibility has been broken (eg removed or changed interfaces)
#    change to C+1:0:0.
# b) If interfaces have been changed or added, but binary compatibility has
#    been preserved, change to C+1:0:A+1
# c) If the interface is the same as the previous version, change to C:R+1:A
LIBRATBAG_LT_VERSION=1:0:0
AC_SUBST(LIBRATBAG_LT_VERSION)

AM_SILENT_RULES([yes])
AC_USE_SYSTEM_EXTENSIONS

# Check for programs
AC_PROG_CC_C99
AC_PROG_CXX # Only used by build C++ test
AC_PROG_GREP

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

PKG_PROG_PKG_CONFIG()
PKG_CHECK_MODULES(LIBUDEV, [libudev])
PKG_CHECK_MODULES(LIBEVDEV, [libevdev])

if test "x$GCC" = "xyes"; then
	GCC_CXXFLAGS="-Wall -Wextra -Wno-unused-parameter -g -fvisibility=hidden"
	GCC_CFLAGS="$GCC_CXXFLAGS -Wmissing-prototypes -Wstrict-prototypes"
fi
AC_SUBST(GCC_CFLAGS)
AC_SUBST(GCC_CXXFLAGS)

AC_ARG_ENABLE([documentation],
	      [AC_HELP_STRING([--enable-documentation],
		              [Enable building the documentation (default=auto)])],
	      [build_documentation="$enableval"],
	      [build_documentation="auto"])

if test "x$build_documentation" = "xyes" -o "x$build_documentation" = "xauto"; then
	AC_PATH_PROG(DOXYGEN, doxygen)
	if test "x$DOXYGEN" = "x"; then
		if test "x$build_documentation" = "xyes"; then
			AC_MSG_ERROR([Documentation build requested but doxygen not found. Install doxygen or disable the documentation using --disable-documentation])
		fi
	else
		AC_MSG_CHECKING([for compatible doxygen version])
		doxygen_version=`$DOXYGEN --version`
		AS_VERSION_COMPARE([$doxygen_version], [1.6.0],
		                   [AC_MSG_RESULT([no])
		                    DOXYGEN=""],
		                   [AC_MSG_RESULT([yes])],
		                   [AC_MSG_RESULT([yes])])
		if test "x$DOXYGEN" = "x" -a "x$build_documentation" = "xyes"; then
			AC_MSG_ERROR([Doxygen $doxygen_version too old. Doxygen 1.6+ required for documentation build. Install required doxygen version or disable the documentation using --disable-documentation])
		fi
	fi

	AC_PATH_PROG(DOT, dot)
	if test "x$DOT" = "x"; then
		if test "x$build_documentation" = "xyes"; then
			AC_MSG_ERROR([Documentation build requested but graphviz's dot not found. Install graphviz or disable the documentation using --disable-documentation])
		fi
	else
		AC_MSG_CHECKING([for compatible dot version])
		dot_version=`$DOT -V 2>&1|$GREP -oP '(?<=version\W)@<:@0-9.@:>@*(?=\W(.*))'`
		AS_VERSION_COMPARE([$dot_version], [2.26.0],
		                   [AC_MSG_RESULT([no])
		                    DOT=""],
		                   [AC_MSG_RESULT([yes])],
		                   [AC_MSG_RESULT([yes])])
		if test "x$DOT" = "x" -a "x$build_documentation" = "xyes"; then
			AC_MSG_ERROR([Graphviz dot $dot_version too old. Graphviz 2.26+ required for documentation build. Install required graphviz version or disable the documentation using --disable-documentation])
		fi
	fi

	if test "x$DOXYGEN" != "x" -a "x$DOT" != "x"; then
		build_documentation="yes"
	else
		build_documentation="no"
	fi
fi

AC_ARG_ENABLE(tests,
	      AS_HELP_STRING([--enable-tests], [Build the tests (default=auto)]),
	      [build_tests="$enableval"],
	      [build_tests="auto"])

PKG_CHECK_MODULES(CHECK, [check >= 0.9.10], [HAVE_CHECK="yes"], [HAVE_CHECK="no"])

if test "x$build_tests" = "xauto"; then
	build_tests="$HAVE_CHECK"
fi
if test "x$build_tests" = "xyes"; then
	if test "x$HAVE_CHECK" = "xno"; then
		AC_MSG_ERROR([Cannot build tests, check is missing])
	fi

	AC_PATH_PROG(VALGRIND, [valgrind])
fi

AC_DEFINE(BUILD_TESTS, [1], ["Build the test suite])
AM_CONDITIONAL(HAVE_VALGRIND, [test "x$VALGRIND" != "x"])
AM_CONDITIONAL(BUILD_TESTS, [test "x$build_tests" = "xyes"])
AM_CONDITIONAL(BUILD_DOCS, [test "x$build_documentation" = "xyes"])

AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 doc/libratbag.doxygen
		 src/Makefile
		 src/libratbag.pc
		 src/libratbag-version.h
		 test/Makefile
		 tools/Makefile])
AC_CONFIG_FILES([test/symbols-leak-test],
		[chmod +x test/symbols-leak-test])
AC_OUTPUT

AC_MSG_RESULT([
	Prefix			${prefix}

	Build documentation	${build_documentation}
	Build tests		${build_tests}
	Tests use valgrind	${VALGRIND}
	])
