#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([aqvision], [0.1], [sebastien@fortas.org])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AM_SILENT_RULES([yes]) 
AC_CONFIG_SRCDIR([src/libvision.h])
#AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_LANG(C++)
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_14([noext], [mandatory])

# Checks for libraries.
AC_CHECK_LIB([boost_filesystem], [main], [], [AC_ERROR([boost_filesystem not found, you probably need to install boost-dev])])
AC_CHECK_LIB([boost_system], [main], [], [AC_ERROR([boost_system not found, you probably need to install boost-dev])])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h sys/ioctl.h syslog.h unistd.h boost/circular_buffer.hpp linux/hiddev.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_CHECK_FUNCS([memset pow strerror])

AC_OUTPUT
AC_OUTPUT(Makefile src/Makefile misc/Makefile)
