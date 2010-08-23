dnl $Id$
dnl config.m4 for extension threading

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

dnl PHP_ARG_WITH(threading, for threading support,
dnl Make sure that the comment is aligned:
dnl [  --with-threading             Include threading support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(threading, whether to enable threading support,
[  --enable-threading           Enable threading support], [yes])

if test "$PHP_THREADING" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-threading -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/threading.h"  # you most likely want to change this
  dnl if test -r $PHP_THREADING/$SEARCH_FOR; then # path given as parameter
  dnl   THREADING_DIR=$PHP_THREADING
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for threading files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       THREADING_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$THREADING_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the threading distribution])
  dnl fi

  dnl # --with-threading -> add include path
  dnl PHP_ADD_INCLUDE($THREADING_DIR/include)

  dnl # --with-threading -> check for lib and symbol presence
  dnl LIBNAME=threading # you may want to change this
  dnl LIBSYMBOL=threading # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $THREADING_DIR/lib, THREADING_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_THREADINGLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong threading lib version or lib not found])
  dnl ],[
  dnl   -L$THREADING_DIR/lib -lm -ldl
  dnl ])
  dnl
  dnl PHP_SUBST(THREADING_SHARED_LIBADD)

  PHP_NEW_EXTENSION(threading, threading.c, $ext_shared)
  CPPFLAGS="$CPPFLAGS -DPTH_SYSCALL_SOFT=1"
fi
