/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2007 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Moriyoshi Koizumi <moriyoshi@php.net>                        |
  | Author: Alec Gorge <alecgorge@gmail.com>                             |
  +----------------------------------------------------------------------+
*/

/* $Id: header,v 1.16.2.1.2.1 2007/01/01 19:32:09 iliaa Exp $ */

#ifndef PHP_THREADING_H
#define PHP_THREADING_H

// if you define this debug messages will appear (mostly relating to thread polling)
//#if _DEBUG
#	define PHP_THREADING_DEBUG
// #	include <vld.h>
//#endif

#define PHP_THREADING_QUESTIONABLE_VARS

extern zend_module_entry threading_module_entry;
#define phpext_threading_ptr &threading_module_entry

#ifdef PHP_WIN32
#define PHP_THREADING_API __declspec(dllexport)
#else
#define PHP_THREADING_API
#endif

#ifdef ZTS
#include "TSRM.h"

PHP_MINIT_FUNCTION(threading);
PHP_MSHUTDOWN_FUNCTION(threading);
PHP_MINFO_FUNCTION(threading);
PHP_RSHUTDOWN_FUNCTION(threading);

PHP_FUNCTION(thread_create);
PHP_FUNCTION(thread_suspend);
PHP_FUNCTION(thread_resume);
PHP_FUNCTION(thread_join);
PHP_FUNCTION(thread_cleanup);
PHP_FUNCTION(thread_kill);
PHP_FUNCTION(thread_mutex_create);
PHP_FUNCTION(thread_mutex_acquire);
PHP_FUNCTION(thread_mutex_release);
PHP_FUNCTION(thread_message_queue_create);
PHP_FUNCTION(thread_message_queue_post);
PHP_FUNCTION(thread_message_queue_poll);
PHP_FUNCTION(thread_message_queue_stop);
PHP_FUNCTION(thread_message_slot_create);
PHP_FUNCTION(thread_message_slot_post);
PHP_FUNCTION(thread_message_slot_subscribe);

typedef struct _php_thread_entry_t php_thread_entry_t;

#define PHP_THREAD_SELF (*(php_thread_entry_t**)(*tsrm_ls)[TSRM_UNSHUFFLE_RSRC_ID(global_ctx.tsrm_id)])

#endif

#ifdef PHP_THREADING_DEBUG
#define THR_DEBUG(v) printf v; fflush(stdout);
#else
#define THR_DEBUG(v)
#endif


#endif	/* PHP_THREADING_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
