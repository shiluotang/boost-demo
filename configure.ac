#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([boost-demo], [1.0], [])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([tests/test-hello.cc])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])

# Checks for programs.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_PROG_CXX
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
m4_ifdef([LT_INIT], [LT_INIT])

# Checks for libraries.
AX_CXX_COMPILE_STDCXX([11], [ext], [mandatory])
AX_BOOST_BASE([1.53])
AX_BOOST_SYSTEM
AX_BOOST_THREAD
AX_BOOST_CHRONO
AX_BOOST_ASIO
PKG_CHECK_MODULES([GTEST], [gtest],,[
                   m4_ifdef(
                            [GTEST_LIB_CHECK],
                            [GTEST_LIB_CHECK([1.6], [HAVE_GTEST=yes], [HAVE_GTEST=no])]
                            )
                   ])

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 tests/Makefile])
AC_OUTPUT
