# ------------------------------------------------------------------------------
# main

AC_PREREQ([2.64])

AC_INIT([ratbagd],
        [0.2],
        [http://www.github.com/libratbag/ratbagd],
        [ratbagd],
        [http://www.github.com/libratbag/ratbagd])

AC_CONFIG_SRCDIR([src/ratbagd.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects parallel-tests])
AM_SILENT_RULES([yes])
AC_DEFINE_UNQUOTED([CANONICAL_HOST], "$host", [Canonical host string.])

LT_PREREQ(2.2)
LT_INIT([disable-static])

AC_PROG_MKDIR_P
AC_PROG_LN_S
AC_PROG_SED
AC_PROG_CC_C99

AC_PATH_PROG([M4], [m4])
AM_PATH_PYTHON

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
# dependencies

PKG_CHECK_MODULES([LIBUDEV], [libudev])
PKG_CHECK_MODULES([LIBSYSTEMD], [libsystemd >= 227])
PKG_CHECK_MODULES([LIBRATBAG], [libratbag >= 0.6])

PKG_CHECK_VAR([libratbag_data_dir], [libratbag], [pkgdatadir],
	      [AC_DEFINE_UNQUOTED([LIBRATBAG_DATA_DIR], ["$libratbag_data_dir"], [Directory for libratbag's SVG files])])

# ------------------------------------------------------------------------------
# options
AC_ARG_WITH([systemd-unit-dir],
	    AS_HELP_STRING([--with-systemd-unit-dir=DIR],
			   [systemd unit directory [[default=$libdir/systemd/system]]]),
	    [unitdir="$withval"],
	    [unitdir="$libdir/systemd/system"])
SYSTEMD_UNIT_DIR=${unitdir}
AC_SUBST(SYSTEMD_UNIT_DIR)

AC_ARG_WITH([dbus-root-dir],
	    AS_HELP_STRING([--with-dbus-root-dir=DIR],
			   [dbus service directory [[default=$datadir/dbus-1]]]),
	    [dbusrootdir="$withval"],
	    [dbusrootdir="$datadir/dbus-1"])
DBUS_SYSTEM_SERVICES_DIR="${dbusrootdir}/system-services"
DBUS_SYSTEM_CONFIG_DIR="${dbusrootdir}/system.d"
AC_SUBST(DBUS_SYSTEM_SERVICES_DIR)
AC_SUBST(DBUS_SYSTEM_CONFIG_DIR)

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
