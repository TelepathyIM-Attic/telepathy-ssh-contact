m4_define(released, 1)
AC_INIT(ssh-contact, 0.3, https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=ssh-contact)
AC_PREREQ(2.59)
AC_COPYRIGHT([
  Copyright (C) 2010 Collabora Ltd.
])

ifelse(released, 1, [], [enable_maintainer_mode="yes"])
AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR(.)
AM_INIT_AUTOMAKE(1.9 dist-bzip2 no-define -Wno-portability)
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
AC_ISC_POSIX
AC_PROG_CC
AC_HEADER_STDC
AM_PROG_LIBTOOL
AM_PROG_MKDIR_P
AM_PATH_GLIB_2_0
AC_PATH_XTRA
IT_PROG_INTLTOOL([0.35.0])

# -----------------------------------------------------------

PKG_CHECK_MODULES(SSH_CONTACT,
[
  telepathy-glib >= 0.11.12
  glib-2.0 >= 2.24
  gio-2.0
  gio-unix-2.0
])

# -----------------------------------------------------------
# Error flags
# -----------------------------------------------------------
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")
AS_COMPILER_FLAG(-Werror, werror=yes, werror=no)

AC_ARG_ENABLE(Werror,
  AC_HELP_STRING([--disable-Werror],[compile without -Werror (normally enabled in development builds)]),
    werror=$enableval, :)

AS_COMPILER_FLAG(-Wextra, wextra=yes, wextra=no)
AS_COMPILER_FLAG(-Wno-missing-field-initializers,
        wno_missing_field_initializers=yes,
        wno_missing_field_initializers=no)
AS_COMPILER_FLAG(-Wno-unused-parameter,
        wno_unused_parameter=yes,
        wno_unused_parameter=no)

ifelse(released, 1, [],
    [
        if test x$werror = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Werror"
        fi
        if test x$wextra = xyes -a \
            x$wno_missing_field_initializers = xyes -a \
            x$wno_unused_parameter = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Wextra -Wno-missing-field-initializers -Wno-unused-parameter"
        fi
    ])

AS_COMPILER_FLAG(-Wdeclaration-after-statement, ERROR_CFLAGS="$ERROR_CFLAGS -Wdeclaration-after-statement")
AS_COMPILER_FLAG(-Wshadow, ERROR_CFLAGS="$ERROR_CFLAGS -Wshadow")
AS_COMPILER_FLAG(-Wmissing-prototypes, ERROR_CFLAGS="$ERROR_CFLAGS -Wmissing-prototypes")
AS_COMPILER_FLAG(-Wmissing-declarations, ERROR_CFLAGS="$ERROR_CFLAGS -Wmissing-declarations")
# Disabled because some GTK+ headers (like gtkitemfactory.h) are bugged :(
#AS_COMPILER_FLAG(-Wstrict-prototypes, ERROR_CFLAGS="$ERROR_CFLAGS -Wstrict-prototypes")

AC_SUBST(ERROR_CFLAGS)

# -----------------------------------------------------------
# Language Support
# -----------------------------------------------------------

GETTEXT_PACKAGE=ssh-contact
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[Gettext package name])

AM_GLIB_GNU_GETTEXT

# -----------------------------------------------------------
# Vinagre plugin
# -----------------------------------------------------------
AC_ARG_ENABLE(vinagre,
              AS_HELP_STRING([--enable-vinagre=@<:@no/yes/auto@:>@],
                             [build with vinagre plugin]), ,
                             enable_vinagre=no)

if test "x$enable_vinagre" != "xno"; then
   PKG_CHECK_MODULES(VINAGRE,
   [
      vinagre-1.0
      vte
   ], have_vinagre="yes", have_vinagre="no")

   if test "x$have_vinagre" = "xyes"; then
      AC_DEFINE(HAVE_VINAGRE, 1, [Define if you have libvinagre])
   fi
else
   have_vinagre=no
fi

if test "x$enable_vinagre" = "xyes" -a "x$have_vinagre" != "xyes"; then
   AC_MSG_ERROR([Could not find vinagre dependencies.])
fi
AM_CONDITIONAL(HAVE_VINAGRE, test "x$have_vinagre" = "xyes")

# -----------------------------------------------------------

AC_OUTPUT([
Makefile
po/Makefile.in
src/Makefile
src/vinagre/Makefile
])
