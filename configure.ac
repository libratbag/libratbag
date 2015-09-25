# ------------------------------------------------------------------------------
# main

AC_PREREQ([2.64])

AC_INIT([ratbagd],
        [1],
        [http://www.github.com/libratbag/ratbagd],
        [ratbagd],
        [http://www.github.com/libratbag/ratbagd])

AC_CONFIG_SRCDIR([src/ratbagd.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AM_MAINTAINER_MODE([enable])
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects parallel-tests])
AM_SILENT_RULES([yes])
AC_CANONICAL_HOST
AC_DEFINE_UNQUOTED([CANONICAL_HOST], "$host", [Canonical host string.])

LT_PREREQ(2.2)
LT_INIT([disable-static])

AC_PROG_MKDIR_P
AC_PROG_LN_S
AC_PROG_SED
AC_PROG_GREP
AC_PROG_AWK
AC_PROG_CC_C99

AC_PATH_PROG([M4], [m4])
AC_PATH_PROG([XSLTPROC], [xsltproc])

AS_IF([! ln --relative --help > /dev/null 2>&1], [AC_MSG_ERROR([*** ln doesn't support --relative ***])])

# ------------------------------------------------------------------------------
# arguments

AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [enable debug options]))

# ------------------------------------------------------------------------------
# toolchain

CC_CHECK_FLAGS_APPEND([with_cflags], [CFLAGS], [\
        -pipe \
        -Wall \
        -Wextra \
        -Wno-inline \
        -Wundef \
        "-Wformat=2 -Wformat-security -Wformat-nonliteral" \
        -Wlogical-op \
        -Wsign-compare \
        -Wmissing-include-dirs \
        -Wold-style-definition \
        -Wpointer-arith \
        -Winit-self \
        -Wdeclaration-after-statement \
        -Wfloat-equal \
        -Wsuggest-attribute=noreturn \
        -Wmissing-prototypes \
        -Wstrict-prototypes \
        -Wredundant-decls \
        -Wmissing-declarations \
        -Wmissing-noreturn \
        -Wendif-labels \
        -Wstrict-aliasing=2 \
        -Wwrite-strings \
        -Wno-long-long \
        -Wno-overlength-strings \
        -Wno-unused-parameter \
        -Wno-missing-field-initializers \
        -Wno-unused-result \
        -Werror=overflow \
        -Wdate-time \
        -Wnested-externs \
        -ffast-math \
        -fno-common \
        -fdiagnostics-show-option \
        -fno-strict-aliasing \
        -fvisibility=hidden \
        -ffunction-sections \
        -fdata-sections \
        -fstack-protector \
        -fstack-protector-strong \
        -fPIE \
        --param=ssp-buffer-size=4])

AS_CASE([$CC], [*clang*],
        [CC_CHECK_FLAGS_APPEND([with_cppflags], [CPPFLAGS], [ \
               -Wno-typedef-redefinition \
               -Wno-gnu-variable-sized-type-not-at-end \
        ])])

CC_CHECK_FLAGS_APPEND([with_ldflags], [LDFLAGS], [\
        -Wl,--as-needed \
        -Wl,--no-undefined \
        -Wl,--gc-sections \
        -Wl,-z,relro \
        -Wl,-z,now \
        -pie])

AS_IF([test "x$enable_debug" = "xyes"],
      [
        CC_CHECK_FLAGS_APPEND([with_cflags], [CFLAGS], [ \
                -g \
                -O0 \
                -ftrapv])
      ], [
        CC_CHECK_FLAGS_APPEND([with_cppflags], [CPPFLAGS], [\
               -Wp,-D_FORTIFY_SOURCE=2])
      ])

AC_SUBST([OUR_CFLAGS], "$with_cflags")
AC_SUBST([OUR_CPPFLAGS], "$with_cppflags")
AC_SUBST([OUR_LDFLAGS], "$with_ldflags")

# ------------------------------------------------------------------------------
# system features

# This makes sure pkg.m4 is available.
m4_pattern_forbid([^_?PKG_[A-Z_]+$],[*** pkg.m4 missing, please install pkg-config])

# ------------------------------------------------------------------------------
# dependencies

PKG_CHECK_MODULES([LIBUDEV], [libudev])
PKG_CHECK_MODULES([LIBSYSTEMD], [libsystemd >= 225])

# ------------------------------------------------------------------------------
# report

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION
        debug:                  ${enable_debug}

        prefix:                 ${prefix}
        exec_prefix:            ${exec_prefix}
        includedir:             ${includedir}
        libdir:                 ${libdir}

        CFLAGS:                 ${OUR_CFLAGS} ${CFLAGS}
        CPPFLAGS:               ${OUR_CPPFLAGS} ${CPPFLAGS}
        LDFLAGS:                ${OUR_LDFLAGS} ${LDFLAGS}
])
