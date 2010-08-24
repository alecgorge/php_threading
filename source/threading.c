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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#ifdef PHP_WIN32
#define HAVE_STRUCT_TIMESPEC
#define HAVE_STRTOK_R
#include "pthread.h"
#endif

#include "php.h"

#ifndef ZTS
#error "This extention requires PHP to be built with ZTS support"
#endif

#ifndef PTHREAD_H
#error "This extention requires PHP to be built with pthread-win32 or equivelent"
#endif

#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_threading.h"
#include "php_main.h"
#include "main/php_network.h"

#ifndef PHP_WIN32
#include <signal.h>
#endif

#define bail_if_fail(X) if ((X) != 0) zend_bailout()

typedef struct _php_thread_message_t php_thread_message_t;

struct _php_thread_entry_t {
	php_thread_entry_t *parent;
	pthread_t t;
	int refcount;
	size_t serial;
	size_t alive_subthread_count;
	struct {
		/* FIXME: this kind of list should be implemented in sparse vector */
		size_t n;
		size_t cap;
		php_thread_entry_t **v;
	} subthreads;
	void ***tsrm_ls;
	void *exit_value;
	unsigned finished:1;
	unsigned destroyed:1;
	pthread_mutex_t gc_mtx;
	php_thread_message_t *garbage;
};

typedef struct _php_thread_global_ctx_t {
	php_thread_entry_t entry;
	pthread_t main_thread;
	int le_thread;
	int le_mutex;
	int le_msg_queue;
	int le_msg_slot;
	int tsrm_id;
} php_thread_global_ctx_t;

enum php_thread_lock_result {
	PHP_THREAD_LOCK_FAILED = -1,
	PHP_THREAD_LOCK_ACQUIRED = 0,
	PHP_THREAD_LOCK_TIMEOUT = 1
};

typedef struct _php_thread_thread_param_t {
	pthread_mutex_t ready_cond_mtx;
	pthread_cond_t ready_cond;
	php_thread_entry_t *entry;
	zend_compiler_globals *compiler_globals;
	zend_executor_globals *executor_globals;
	zend_fcall_info callable;
	int status;
} php_thread_thread_param_t;

typedef struct _php_thread_mutex_t {
	php_thread_entry_t *owner;
	pthread_mutex_t m;
	int refcount;
} php_thread_mutex_t;

struct _php_thread_message_t {
	zval *value;
	php_thread_entry_t *thd;
	php_thread_message_t *next;
	php_thread_message_t *prev;
};

typedef struct _php_thread_message_queue_t {
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	php_thread_message_t *first;
	php_thread_message_t *last;
	int refcount;
} php_thread_message_queue_t;

typedef struct _php_thread_message_sender_t php_thread_message_sender_t;

struct _php_thread_message_sender_t {
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	zval *value;
	void ***tsrm_ls;
	php_thread_message_sender_t *prev;
	php_thread_message_sender_t *next;
};

typedef struct _php_thread_message_slot_t {
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	php_thread_message_sender_t *first;
	php_thread_message_sender_t *last;
	zval *value;
	void ***tsrm_ls;
	int nsubs;
	int refcount;
} php_thread_message_slot_t;

typedef void *(*php_thread_rsrc_clone_fn_t)(const void *src, int persistent, void ***prev_tsrm_ls TSRMLS_DC);

typedef struct _php_thread_rsrc_desc_t {
	int id;
	unsigned persistent:1;
	size_t size;
	php_thread_rsrc_clone_fn_t clone;
} php_thread_rsrc_desc_t;

static php_thread_global_ctx_t global_ctx;
static int php_thread_do_exit = 0;

ZEND_BEGIN_ARG_INFO_EX(arginfo_thread_create, 0, 0, 1)
	ZEND_ARG_INFO(0, callable)
	ZEND_ARG_INFO(0, ...)
ZEND_END_ARG_INFO()

/* {{{ threading_functions[]
 *
 * Every user visible function must have an entry in threading_functions[].
 */
zend_function_entry threading_functions[] = {
	PHP_FE(thread_create,	arginfo_thread_create)
	PHP_FE(thread_suspend,	NULL)
	PHP_FE(thread_resume,	NULL)
	PHP_FE(thread_join,	NULL)
	PHP_FE(thread_mutex_create,	NULL)
	PHP_FE(thread_mutex_acquire,	NULL)
	PHP_FE(thread_mutex_release,	NULL)
	PHP_FE(thread_message_queue_create,	NULL)
	PHP_FE(thread_message_queue_post,	NULL)
	PHP_FE(thread_message_queue_poll,	NULL)
	PHP_FE(thread_message_queue_stop,	NULL)
	PHP_FE(thread_message_slot_create,	NULL)
	PHP_FE(thread_message_slot_post,	NULL)
	PHP_FE(thread_message_slot_subscribe,	NULL)
	{NULL, NULL, NULL}	/* Must be the last line in threading_functions[] */
};
/* }}} */

/* {{{ threading_module_entry
 */
zend_module_entry threading_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"threading",
	threading_functions,
	PHP_MINIT(threading),
	PHP_MSHUTDOWN(threading),
	NULL,
	PHP_RSHUTDOWN(threading),
	PHP_MINFO(threading),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_THREADING
ZEND_GET_MODULE(threading)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("threading.global_value",	  "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_threading_globals, threading_globals)
	STD_PHP_INI_ENTRY("threading.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_threading_globals, threading_globals)
PHP_INI_END()
*/
/* }}} */

static void php_thread_entry_dispose(php_thread_entry_t **entry TSRMLS_DC);

static int php_thread_convert_object_ref(zval **retval, zval *src,
		void ***prev_tsrm_ls TSRMLS_DC);

/* {{{ php_thread_entry_addref() */
static void php_thread_entry_addref(php_thread_entry_t *entry)
{
	++entry->refcount;
}
/* }}} */

/* {{{ php_thread_entry_join() */
static int php_thread_entry_join(php_thread_entry_t *entry,
		zval **retval TSRMLS_DC)
{
	if (entry == PHP_THREAD_SELF) {
		return FAILURE;
	}
	THR_DEBUG(("calling php_thread_entry_addref in php_thread_entry_join\n"));
	php_thread_entry_addref(entry);
	if (!entry->finished) {
		zval *tmp;
		THR_DEBUG(("calling pthread_join in php_thread_entry_join\n"));
		bail_if_fail(pthread_join(entry->t, (void **)&tmp));
		THR_DEBUG(("calling assert(entry->finished) in php_thread_entry_join\n"));
		assert(entry->finished);
		if (retval && tmp != NULL) {
			*retval = tmp;
		}
	} else {
		if(retval) {
			retval = NULL;
		}
	}
	THR_DEBUG(("calling php_thread_entry_dispose in php_thread_entry_join\n"));
	php_thread_entry_dispose(&entry TSRMLS_CC);
	THR_DEBUG(("returning in php_thread_entry_join\n"));
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_entry_wait() */
static void php_thread_entry_wait(php_thread_entry_t *entry TSRMLS_DC)
{
	php_thread_entry_t **p, **e = entry->subthreads.v + entry->subthreads.n;
	for (p = entry->subthreads.v; p < e; ++p) {
		if (!*p) {
			continue;
		}
		php_thread_entry_join(*p, NULL TSRMLS_CC);
	}
	assert(entry->alive_subthread_count == 0);
}
/* }}} */

/* {{{ php_thread_entry_suspend() */
static int php_thread_entry_suspend(php_thread_entry_t *entry)
{
	if (entry->destroyed) {
		return FAILURE;
	}
	return FAILURE;
}
/* }}} */

/* {{{ php_thread_entry_resume() */
static int php_thread_entry_resume(php_thread_entry_t *entry)
{
	if (entry->destroyed) {
		return FAILURE;
	}
	return FAILURE;
}
/* }}} */

/* {{{ php_thread_entry_clone() */
static php_thread_entry_t *php_thread_entry_clone(
		const php_thread_entry_t *src, int persistent,
		void ***prev_tsrm_ls TSRMLS_DC)
{
	return (php_thread_entry_t *)src;
}
/* }}} */

/* {{{ php_thread_entry_cancel() */
static void php_thread_entry_cancel(php_thread_entry_t *entry TSRMLS_DC)
{
	if (!entry->finished) {
		pthread_cancel(entry->t);
		entry->finished = 1;
		--entry->parent->alive_subthread_count;
		php_thread_entry_dispose(&entry TSRMLS_CC);
	}
}
/* }}} */

/* {{{ php_thread_entry_dtor() */
static void php_thread_entry_dtor(php_thread_entry_t *entry TSRMLS_DC)
{
	if (entry->destroyed) {
		return;
	}
	if (!entry->finished) {
		php_thread_entry_cancel(entry TSRMLS_CC);
	}
	if (entry->subthreads.v) {
		php_thread_entry_t **p;
		php_thread_entry_t **e = entry->subthreads.v
				+ entry->subthreads.n;
		for (p = entry->subthreads.v; p < e; ++p) {
			php_thread_entry_dispose(p TSRMLS_CC);
		}
		pefree(entry->subthreads.v, 1);
	}

	entry->destroyed = 1;
}
/* }}} */

/* {{{ php_thread_entry_dispose() */
static void php_thread_entry_dispose(php_thread_entry_t **entry TSRMLS_DC)
{
	if (!*entry) {
		return;
	}

	--(*entry)->refcount;
	if ((*entry)->refcount <= 0) {
		php_thread_entry_t *parent = (*entry)->parent;
		size_t serial = (*entry)->serial;
		php_thread_entry_dtor(*entry TSRMLS_CC);
		pefree(*entry, 1);
		if (parent) {
			parent->subthreads.v[serial] = NULL;
		}
	}
}
/* }}} */

/* {{{ php_thread_entry_ctor() */
static int php_thread_entry_ctor(php_thread_entry_t *entry,
		php_thread_entry_t *parent, size_t serial)
{
	pthread_t tempThread;
	entry->serial = serial;
	entry->alive_subthread_count = 0;
	entry->subthreads.v = NULL;
	entry->subthreads.cap = 0;
	entry->subthreads.n = 0;
	entry->finished = 0;
	entry->destroyed = 0;
	entry->refcount = 1;
	entry->t = tempThread;
	entry->parent = parent;
	if (pthread_mutex_init(&entry->gc_mtx, NULL) != 0) {
		return FAILURE;
	}
	entry->garbage = NULL;
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_mutex_acquire() */
static enum php_thread_lock_result
php_thread_mutex_acquire(php_thread_mutex_t *mtx, double timeout TSRMLS_DC)
{
	int retval;
	struct timespec future;

	/* Getting system time in seconds */
#ifdef PHP_WIN32
	SYSTEMTIME st;
#else
	struct timespec st;
#endif

#ifdef PHP_WIN32
	GetSystemTime(&st);
	future.tv_sec = st.wSecond + (long)timeout;
	future.tv_nsec = st.wMilliseconds + (long)(timeout - (double)(long)timeout);
#else
	gettimeofday(&st, NULL);
	future.tv_sec = st.tv_sec + (long)timeout;
	future.tv_nsec = st.tv_nsec + (long)(timeout - (double)(long)timeout);
#endif

	if (timeout >= 0) {
		retval = pthread_mutex_timedlock(&mtx->m, &future);
	}
	retval = pthread_mutex_lock(&mtx->m);

	if (!retval) {
		mtx->owner = PHP_THREAD_SELF;
		return PHP_THREAD_LOCK_ACQUIRED;
	}
	return retval == ETIMEDOUT ? PHP_THREAD_LOCK_TIMEOUT: PHP_THREAD_LOCK_FAILED;
}
/* }}} */

/* {{{ php_thread_mutex_release() */
static int php_thread_mutex_release(php_thread_mutex_t *mtx TSRMLS_DC)
{
	if (PHP_THREAD_SELF != mtx->owner) {
		return FAILURE;
	}
	return pthread_mutex_unlock(&mtx->m) ? SUCCESS: FAILURE;
}
/* }}} */

/* {{{ php_thread_mutex_addref() */
static void php_thread_mutex_addref(php_thread_mutex_t *mtx)
{
	++mtx->refcount;
}
/* }}} */

/* {{{ php_thread_mutex_clone() */
static php_thread_mutex_t *php_thread_mutex_clone(
		const php_thread_mutex_t *src, int persistent,
		void ***prev_tsrm_ls TSRMLS_DC)
{
	php_thread_mutex_addref((php_thread_mutex_t *)src);
	return (php_thread_mutex_t *)src;
}
/* }}} */

/* {{{ php_thread_mutex_dtor() */
static void php_thread_mutex_dtor(php_thread_mutex_t *mtx TSRMLS_DC)
{
	if (PHP_THREAD_SELF == mtx->owner) {
		pthread_mutex_unlock(&mtx->m);
		mtx->owner = NULL;
	}
}
/* }}} */

/* {{{ php_thread_mutex_dispose() */
static void php_thread_mutex_dispose(php_thread_mutex_t **mtx TSRMLS_DC)
{
	if (!*mtx) {
		return;
	}
	--(*mtx)->refcount;
	if ((*mtx)->refcount <= 0) {
		php_thread_mutex_dtor(*mtx TSRMLS_CC);
		pefree(*mtx, 1);
	}
}
/* }}} */

/* {{{ php_thread_mutex_ctor() */
static int php_thread_mutex_ctor(php_thread_mutex_t *mtx)
{
	if (pthread_mutex_init(&mtx->m, NULL) != 0) {
		return FAILURE;
	}
	mtx->refcount = 1;
	mtx->owner = NULL;
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_queue_post */
static int php_thread_message_queue_post(php_thread_message_queue_t *queue, zval *value TSRMLS_DC)
{
	php_thread_message_t *msg = pemalloc(sizeof(php_thread_message_t), 1);
	if (!msg) {
		return FAILURE;
	}
	msg->value = value;
	msg->next = NULL;

	if (pthread_mutex_lock(&queue->mtx) != 0) {
		pefree(msg, 1);
		return FAILURE;
	}
	if (!queue->last) {
		queue->first = msg;
	} else {
		queue->last->next = msg;
	}
	msg->prev = queue->last;
	msg->thd = PHP_THREAD_SELF;
	queue->last = msg;

	Z_ADDREF_P(value);
	bail_if_fail(pthread_cond_signal(&queue->cond));
	bail_if_fail(pthread_mutex_unlock(&queue->mtx));
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_queue_poll */
static int php_thread_message_queue_stop(php_thread_message_queue_t *queue TSRMLS_DC)
{
	//php_thread_message_t *msg;
	THR_DEBUG(("--attempting to stop signal from php_thread_message_queue_stop\n"));
	THR_DEBUG(("--%d\n", pthread_mutex_unlock(&queue->mtx)));
	php_thread_do_exit = 1;
	return (pthread_cond_broadcast(&queue->cond) == 0 ? SUCCESS : FAILURE);
	/*THR_DEBUG(("calling pthread_mutex_lock(&queue->mtx) in php_thread_message_queue_poll\n"));
	if (pthread_mutex_lock(&queue->mtx) != 0) {
		return FAILURE;
	}

	THR_DEBUG(("waiting to be first in the queue\n"));
	while (!queue->first) {
		THR_DEBUG(("pthread_cond_wait(&queue->cond, &queue->mtx) in php_thread_message_queue_poll\n"));
		if (pthread_cond_wait(&queue->cond, &queue->mtx)  != 0) {
			bail_if_fail(pthread_mutex_unlock(&queue->mtx));
			return FAILURE;
		}
	}
	THR_DEBUG(("done waiting\n"));

	msg = queue->first;
	if (msg->prev) {
		msg->prev->next = msg->next;
	} else {
		queue->first = msg->next;
	}
	if (msg->next) {
		msg->next->prev = msg->prev;
	} else {
		queue->last = msg->prev;
	}

	if (SUCCESS != php_thread_convert_object_ref(retval, msg->value,
			msg->thd->tsrm_ls TSRMLS_CC)) {
		ALLOC_INIT_ZVAL(*retval);
	}

	bail_if_fail(pthread_mutex_lock(&msg->thd->gc_mtx));
	msg->next = NULL;
	msg->prev = msg->thd->garbage;
	msg->thd->garbage = msg;
	bail_if_fail(pthread_mutex_unlock(&msg->thd->gc_mtx));

	bail_if_fail(pthread_mutex_unlock(&queue->mtx));

	pthread_kill(msg->thd->t, 31);*/
}
/* }}} */

/* {{{ php_thread_message_queue_poll */
static int php_thread_message_queue_poll(php_thread_message_queue_t *queue, zval **retval TSRMLS_DC)
{
	php_thread_message_t *msg;
	zval *break_return_val;
	char *string_contents = "PHP_THREAD_POLL_STOP";

	/*THR_DEBUG(("php_thread_do_exit: %d\n", php_thread_do_exit));
	if(php_thread_do_exit == 1) {
		pthread_mutex_unlock(&queue->mtx);
		pthread_kill(msg->thd->t, 31);
		return SUCCESS;
	}*/
	THR_DEBUG(("calling pthread_mutex_lock(&queue->mtx) in php_thread_message_queue_poll\n"));
	if (pthread_mutex_lock(&queue->mtx) != 0) {
		return FAILURE;
	}

	THR_DEBUG(("waiting to be first in the queue\n"));
	while (!queue->first) {
		//THR_DEBUG(("php_thread_do_exit: %d\n", php_thread_do_exit));

		if(php_thread_do_exit == 1) {
			pthread_mutex_unlock(&queue->mtx);
			MAKE_STD_ZVAL(break_return_val);
			ZVAL_STRING(break_return_val, string_contents, 1);
			*retval = break_return_val;
			return SUCCESS;
		}
		THR_DEBUG(("pthread_cond_wait(&queue->cond, &queue->mtx) in php_thread_message_queue_poll\n"));
		if (pthread_cond_wait(&queue->cond, &queue->mtx)  != 0) {
			bail_if_fail(pthread_mutex_unlock(&queue->mtx));
			return FAILURE;
		}
	}
	THR_DEBUG(("done waiting\n"));

	msg = queue->first;
	if (msg->prev) {
		msg->prev->next = msg->next;
	} else {
		queue->first = msg->next;
	}
	if (msg->next) {
		msg->next->prev = msg->prev;
	} else {
		queue->last = msg->prev;
	}

	if (SUCCESS != php_thread_convert_object_ref(retval, msg->value,
			msg->thd->tsrm_ls TSRMLS_CC)) {
		ALLOC_INIT_ZVAL(*retval);
	}

	bail_if_fail(pthread_mutex_lock(&msg->thd->gc_mtx));
	msg->next = NULL;
	msg->prev = msg->thd->garbage;
	msg->thd->garbage = msg;
	bail_if_fail(pthread_mutex_unlock(&msg->thd->gc_mtx));

	bail_if_fail(pthread_mutex_unlock(&queue->mtx));

	pthread_kill(msg->thd->t, 31);

	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_queue_addref */
static void php_thread_message_queue_addref(php_thread_message_queue_t *queue)
{
	++queue->refcount;
}
/* }}} */

/* {{{ php_thread_message_queue_clone() */
static php_thread_message_queue_t *php_thread_message_queue_clone(
		const php_thread_message_queue_t *src, int persistent,
		void ***prev_tsrm_ls TSRMLS_DC)
{
	php_thread_message_queue_addref((php_thread_message_queue_t *)src);
	return (php_thread_message_queue_t *)src;
}
/* }}} */

/* {{{ php_thread_message_queue_dtor() */
static void php_thread_message_queue_dtor(php_thread_message_queue_t *queue)
{
}
/* }}} */

/* {{{ php_thread_message_queue_ctor() */
static int php_thread_message_queue_ctor(php_thread_message_queue_t *queue)
{
	if (pthread_mutex_init(&queue->mtx, NULL) != 0) {
		return FAILURE;
	}
	if (pthread_cond_init(&queue->cond, NULL) != 0) {
		return FAILURE;
	}
	queue->first = queue->last = NULL;
	queue->refcount = 1;
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_queue_dispose */
static void php_thread_message_queue_dispose(php_thread_message_queue_t **queue TSRMLS_DC)
{
	if (!*queue) {
		return;
	}

	--(*queue)->refcount;
	if ((*queue)->refcount <= 0) {
		php_thread_message_queue_dtor(*queue);
		pefree(*queue, 1);
	}
}
/* }}} */

/* {{{ php_thread_message_slot_enqueue_self_and_sleep */
static int php_thread_message_slot_enqueue_self_and_sleep(php_thread_message_slot_t *slot, zval *value TSRMLS_DC)
{
	php_thread_message_sender_t *sender = pemalloc(sizeof(php_thread_message_sender_t), 1);
	if (!sender) {
		return FAILURE;
	}

	if (pthread_mutex_init(&sender->mtx, NULL) != 0) {
		pefree(sender, 1);
		return FAILURE;
	}

	if (pthread_cond_init(&sender->cond, NULL) != 0) {
		pefree(sender, 1);
		return FAILURE;
	}

	sender->value = value;
	sender->tsrm_ls = tsrm_ls;

	if (pthread_mutex_lock(&slot->mtx) != 0) {
		pefree(sender, 1);
		return FAILURE;
	}

	if (slot->last) {
		slot->last->next = sender;
	} else {
		slot->first = sender;
	}
	sender->prev = slot->last;
	sender->next = NULL;
	slot->last = sender;

	bail_if_fail(pthread_mutex_unlock(&slot->mtx));

	if (pthread_mutex_lock(&sender->mtx) != 0) {
		return FAILURE;
	}
	if (pthread_cond_wait(&sender->cond, &sender->mtx) != 0) {
		bail_if_fail(pthread_mutex_unlock(&sender->mtx));
		return FAILURE;
	}
	bail_if_fail(pthread_mutex_unlock(&sender->mtx));
	if (!sender->next && !sender->prev)
		pefree(sender, 1);

	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_slot_post */
static int php_thread_message_slot_post(php_thread_message_slot_t *slot, zval *value TSRMLS_DC)
{
	if (slot->nsubs == 0) {
		return php_thread_message_slot_enqueue_self_and_sleep(slot, value TSRMLS_CC);
	}

	if (pthread_mutex_lock(&slot->mtx) != 0) {
		zval_ptr_dtor(&value);
		return FAILURE;
	}
	Z_ADDREF_P(value);
	slot->value = value;
	slot->tsrm_ls = tsrm_ls;
	bail_if_fail(pthread_cond_broadcast(&slot->cond));
	bail_if_fail(pthread_mutex_unlock(&slot->mtx));
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_slot_subscribe */
static int php_thread_message_slot_subscribe(php_thread_message_slot_t *slot, zval **retval TSRMLS_DC)
{
	//php_thread_message_t *msg;
	if (pthread_mutex_lock(&slot->mtx) != 0) {
		return FAILURE;
	}
	if (slot->nsubs == 0 && slot->first) {
		php_thread_message_sender_t *sender = slot->first;

		if (SUCCESS != php_thread_convert_object_ref(retval, sender->value,
				sender->tsrm_ls TSRMLS_CC)) {
			ALLOC_INIT_ZVAL(*retval);
		}
		if (sender->next) {
			sender->next->prev = sender->prev;
		} else {
			slot->last = sender->prev;
		}
		if (sender->prev) {
			sender->prev->next = sender->next;
		} else {
			slot->first = sender->next;
		}
		sender->next = sender->prev = NULL;
		bail_if_fail(pthread_mutex_lock(&sender->mtx));
		bail_if_fail(pthread_cond_signal(&sender->cond));
		bail_if_fail(pthread_mutex_unlock(&sender->mtx));
		bail_if_fail(pthread_mutex_unlock(&slot->mtx));
		return SUCCESS;
	}
	slot->nsubs++;
	if (pthread_cond_wait(&slot->cond, &slot->mtx) != 0) {
		bail_if_fail(pthread_mutex_unlock(&slot->mtx));
		return FAILURE;
	}
	bail_if_fail(pthread_mutex_unlock(&slot->mtx));

	if (SUCCESS != php_thread_convert_object_ref(retval, slot->value,
			slot->tsrm_ls TSRMLS_CC)) {
		ALLOC_INIT_ZVAL(*retval);
	}

	if (slot->nsubs == 0) {
		zval_ptr_dtor(&slot->value);
	}

	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_slot_addref */
static void php_thread_message_slot_addref(php_thread_message_slot_t *slot)
{
	++slot->refcount;
}
/* }}} */

/* {{{ php_thread_message_slot_clone() */
static php_thread_message_slot_t *php_thread_message_slot_clone(
		const php_thread_message_slot_t *src, int persistent,
		void ***prev_tsrm_ls TSRMLS_DC)
{
	php_thread_message_slot_addref((php_thread_message_slot_t *)src);
	return (php_thread_message_slot_t *)src;
}
/* }}} */

/* {{{ php_thread_message_slot_dtor() */
static void php_thread_message_slot_dtor(php_thread_message_slot_t *slot)
{
}
/* }}} */

/* {{{ php_thread_message_slot_ctor() */
static int php_thread_message_slot_ctor(php_thread_message_slot_t *slot)
{
	if (pthread_mutex_init(&slot->mtx, NULL) != 0) {
		return FAILURE;
	}
	if (pthread_cond_init(&slot->cond, NULL) != 0) {
		return FAILURE;
	}
	slot->first = slot->last = NULL;
	slot->nsubs = 0;
	slot->refcount = 1;
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_message_slot_dispose */
static void php_thread_message_slot_dispose(php_thread_message_slot_t **slot TSRMLS_DC)
{
	if (!*slot) {
		return;
	}

	--(*slot)->refcount;
	if ((*slot)->refcount <= 0) {
		php_thread_message_slot_dtor(*slot);
		pefree(*slot, 1);
	}
}
/* }}} */

/* {{{ php_thread_executor_globals_reinit() */
static void php_thread_executor_globals_reinit(zend_executor_globals *dest,
		zend_executor_globals *src)
{
	dest->current_module = src->current_module;
}
/* }}} */
void zend_class_add_ref(zend_class_entry **ce)
{
	(*ce)->refcount++;
}
/* {{{ php_thread_compiler_globals_reinit() */
static void php_thread_compiler_globals_reinit(zend_compiler_globals *dest,
		zend_compiler_globals *src)
{
	zend_hash_clean(dest->function_table);
	zend_hash_copy(dest->function_table, src->function_table,
			(copy_ctor_func_t)function_add_ref, NULL,
			sizeof(zend_function));
	zend_hash_clean(dest->class_table);
	zend_hash_copy(dest->class_table, src->class_table,
			(copy_ctor_func_t)zend_class_add_ref, NULL,
			sizeof(zend_class_entry*));
}
/* }}} */

/* {{{ php_thread_get_stream_persistent_id() */
static const char *php_thread_get_stream_persistent_id(const php_stream *strm TSRMLS_DC)
{
	HashPosition pos;
	HashTable *persistent_list = &EG(persistent_list);
	zend_hash_key key;
	int key_type;
	for (zend_hash_internal_pointer_reset_ex(persistent_list, &pos);
			HASH_KEY_NON_EXISTANT != (key_type = zend_hash_get_current_key_ex(
				persistent_list, &key.arKey,
				&key.nKeyLength, &key.h, 0, &pos));
			zend_hash_move_forward_ex(persistent_list, &pos)) {
		zend_rsrc_list_entry *i;
		zend_hash_get_current_data_ex(persistent_list, (void**)&i, &pos);
		if (i->ptr == strm) {
			return key.arKey;
		}
	}
	return NULL;
}
/* }}} */

/* {{{ php_thread_stream_data_copy_ctor() */
static int php_thread_netstream_data_copy_ctor(php_netstream_data_t *self,
		const php_netstream_data_t *src, int persistent,
		void ***prev_tsrm_ls TSRMLS_DC)
{
	*self = *src;
	self->socket = _dup(self->socket);
	if (self->socket == -1)
		return FAILURE;
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_stream_basic_clone() */
static php_stream *php_thread_stream_basic_clone(
		const php_stream *src, int persistent, void ***prev_tsrm_ls TSRMLS_DC)
{
	php_stream *retval;

	retval = pemalloc(sizeof(*retval), persistent);
	if (!retval) {
		return NULL;
	}

	if (src->readbuf) {
		retval->readbuf = pemalloc(src->readbuflen, persistent);
		if (!retval->readbuf) {
			pefree(retval, persistent);
			return NULL;
		}
		memcpy(retval->readbuf, src->readbuf, src->readbuflen);
	} else {
		retval->readbuf = NULL;
	}

	if (src->orig_path) {
		retval->orig_path = pestrdup(src->orig_path, persistent);
		if (!retval->orig_path) {
			pefree(retval->readbuf, persistent);
			pefree(retval, persistent);
			return NULL;
		}
	} else {
		retval->orig_path = NULL;
	}

	retval->readbuflen = src->readbuflen;
	retval->ops = src->ops;
	retval->flags = src->flags;
	retval->chunk_size = src->chunk_size;
	retval->is_persistent = persistent;
	retval->abstract = NULL;
	retval->wrapperthis = NULL;
	retval->wrapperdata = NULL;
	retval->context = NULL;
	retval->rsrc_id = 0;
	retval->in_free = 0;
	retval->stdiocast = NULL;
	retval->readfilters.stream = retval;
	retval->writefilters.stream = retval;

	memcpy(retval->mode, src->mode, sizeof(retval->mode));

	return retval;
}
/* }}} */

/* {{{ php_thread_stream_clone() *
static php_stream *php_thread_stream_clone(
		const php_stream *src, int persistent, void ***prev_tsrm_ls TSRMLS_DC)
{
	php_stream *retval = NULL;
	php_netstream_data_t *data;

	if (src->ops == &php_stream_socket_ops) {
		retval = php_thread_stream_basic_clone(src, persistent, prev_tsrm_ls
				TSRMLS_CC);
		if (!retval) {
			return NULL;
		}
		data = pemalloc(sizeof(*data), persistent);
		if (!data) {
			php_stream_free(retval, 0);
			return NULL;
		}
		if (FAILURE == php_thread_netstream_data_copy_ctor(data,
				(php_netstream_data_t*)src->abstract, persistent,
				prev_tsrm_ls TSRMLS_CC)) {
			pefree(data, persistent);
			php_stream_free(retval, 0);
			return NULL;
		}
		retval->abstract = data;
	} else if (src->ops == &php_stream_stdio_ops) {
		int fd;
		const char *persistent_id = NULL;
		if (_php_stream_cast((php_stream *)src,
					PHP_STREAM_AS_FD, (void **)&fd, 0, prev_tsrm_ls) == FAILURE) {
			return NULL;
		}
		fd = _dup(fd);
		if (fd == -1) {
			return NULL;
		}
		if (persistent) {
			persistent_id = php_thread_get_stream_persistent_id(src, prev_tsrm_ls);
			if (!persistent_id) {
				return NULL;
			}
		}
		retval = php_stream_fopen_from_fd(fd, src->mode, persistent_id);
	}
	return retval;
}
/* }}} */

/* {{{ php_thread_get_rsrc_desc() */
static int php_thread_get_rsrc_desc(php_thread_rsrc_desc_t *retval, int le_id)
{
	retval->id = le_id;
	retval->persistent = 0;

	if (le_id == php_file_le_stream()) {
		retval->size = sizeof(php_stream);
		/* REMEMBER TO ENABLE */
		// retval->clone = (php_thread_rsrc_clone_fn_t)php_thread_stream_clone;
	} else if (le_id == php_file_le_pstream()) {
		retval->size = sizeof(php_stream);
		retval->persistent = 1;
		/* REMEMBER TO ENABLE */
		// retval->clone = (php_thread_rsrc_clone_fn_t)php_thread_stream_clone;
	} else if (le_id == global_ctx.le_thread) {
		retval->size = sizeof(php_thread_entry_t);
		retval->persistent = 0;
		retval->clone = (php_thread_rsrc_clone_fn_t)php_thread_entry_clone;
	} else if (le_id == global_ctx.le_mutex) {
		retval->size = sizeof(php_thread_mutex_t);
		retval->persistent = 0;
		retval->clone = (php_thread_rsrc_clone_fn_t)php_thread_mutex_clone;
	} else if (le_id == global_ctx.le_msg_queue) {
		retval->size = sizeof(php_thread_message_queue_t);
		retval->persistent = 0;
		retval->clone = (php_thread_rsrc_clone_fn_t)php_thread_message_queue_clone;
	} else if (le_id == global_ctx.le_msg_slot) {
		retval->size = sizeof(php_thread_message_slot_t);
		retval->persistent = 0;
		retval->clone = (php_thread_rsrc_clone_fn_t)php_thread_message_slot_clone;
	} else {
		return FAILURE;
	}
	return SUCCESS;
}
/* }}} */

/* {{{ php_thread_clone_resource() */
static void *php_thread_clone_resource(const php_thread_rsrc_desc_t *desc,
		void *ptr, void ***prev_tsrm_ls TSRMLS_DC)
{
	assert(desc->clone);
	return desc->clone(ptr, desc->persistent, prev_tsrm_ls TSRMLS_CC);
}
/* }}} */

/* {{{ php_thread_convert_object_refs_in_hash() */
static HashTable *php_thread_convert_object_refs_in_hash(HashTable *src,
		void ***prev_tsrm_ls TSRMLS_DC)
{
	HashTable *retval;
	HashPosition pos;
	zend_hash_key key;
	int key_type;

	ALLOC_HASHTABLE(retval);
	zend_hash_init(retval, 0, NULL, ZVAL_PTR_DTOR, 0);

	for (zend_hash_internal_pointer_reset_ex(src, &pos);
			HASH_KEY_NON_EXISTANT != (
				key_type = zend_hash_get_current_key_ex(src, &key.arKey,
					&key.nKeyLength, &key.h, 0, &pos));
			zend_hash_move_forward_ex(src, &pos)) {
		zval **orig;
		zval *val;
		zend_hash_get_current_data_ex(src, (void **)&orig, &pos);
		if (FAILURE == php_thread_convert_object_ref(&val, *orig,
					prev_tsrm_ls TSRMLS_CC)) {
			zend_hash_destroy(retval);
			FREE_HASHTABLE(retval);
			return NULL;
		}
		zend_hash_quick_add(retval, key.arKey,
				key_type == HASH_KEY_IS_LONG ? 0: key.nKeyLength, key.h, &val,
				sizeof(zval*), NULL);
	}
	return retval;
}
/* }}} */

/* {{{ php_thread_convert_object_ref() */
static int php_thread_convert_object_ref(zval **retval, zval *src,
		void ***prev_tsrm_ls TSRMLS_DC)
{
	ALLOC_ZVAL(*retval);
	if (Z_ISREF_P(src)) {
		(*retval)->type = IS_NULL;
	} else {
		if (Z_TYPE_P(src) == IS_OBJECT) {
			zend_class_entry *ce;
			HashTable *props;
			if (!Z_OBJ_HT_P(src)->get_class_entry
					|| !Z_OBJ_HT_P(src)->get_properties) {
				goto fail;
			}
			ce = Z_OBJ_HT_P(src)->get_class_entry(src, prev_tsrm_ls);
			ce = zend_fetch_class(ce->name, ce->name_length, 0 TSRMLS_CC);
			if (ce == NULL) {
				goto fail;
			}
			props = Z_OBJ_HT_P(src)->get_properties(src, prev_tsrm_ls);
			if (!props) {
				goto fail;
			}
			props = php_thread_convert_object_refs_in_hash(props,
					prev_tsrm_ls TSRMLS_CC);
			(*retval)->type = IS_OBJECT;
			if (FAILURE == object_and_properties_init(*retval, ce, props)) {
				goto fail;
			}
		} else if (src->type == IS_ARRAY) {
			(*retval)->type = IS_ARRAY;
			(*retval)->value.ht = php_thread_convert_object_refs_in_hash(
					src->value.ht, prev_tsrm_ls TSRMLS_CC);
			if (!(*retval)->value.ht) {
				goto fail;
			}
		} else if (src->type == IS_RESOURCE) {
			int id;
			zend_rsrc_list_entry le;
			php_thread_rsrc_desc_t desc;
			void *rsrc_ptr;
			rsrc_ptr = _zend_list_find(src->value.lval, &le.type, prev_tsrm_ls);
			if (!rsrc_ptr) {
				goto fail;
			}
			if (FAILURE == php_thread_get_rsrc_desc(&desc, le.type)) {
				goto fail;
			}
			le.ptr = php_thread_clone_resource(&desc, rsrc_ptr, prev_tsrm_ls
					TSRMLS_CC);
			if (!le.ptr) {
				goto fail;
			}
			php_stream_auto_cleanup((php_stream*)le.ptr);
			id = zend_hash_next_free_element(&EG(regular_list));
			zend_hash_index_update(&EG(regular_list), id, &le,
					sizeof(le), NULL);
			(*retval)->type = IS_RESOURCE;
			(*retval)->value.lval = id;
		} else {
			**retval = *src;
			zval_copy_ctor(*retval);
		}
	}
	Z_SET_REFCOUNT_P(*retval, 1);
	Z_UNSET_ISREF_P(*retval);
	return SUCCESS;
fail:
	FREE_ZVAL(*retval);
	return FAILURE;
}
/* }}} */

/* {{{ _php_thread_entry_func() */
static void *_php_thread_entry_func(php_thread_thread_param_t *param)
{
	TSRMLS_FETCH();
	php_thread_entry_t *entry = param->entry;
	zend_fcall_info callable = param->callable;
	zval **args;
	zval* retval = NULL;
	u_int i;

	if (FAILURE == php_request_startup(TSRMLS_C)) {
		bail_if_fail(pthread_mutex_lock(&param->ready_cond_mtx));
		bail_if_fail(pthread_cond_signal(&param->ready_cond));
		bail_if_fail(pthread_mutex_unlock(&param->ready_cond_mtx));
		return NULL;
	}

	entry->tsrm_ls = tsrm_ls;

	++entry->parent->alive_subthread_count;
	php_thread_entry_addref(entry);

	callable.function_name = NULL;
	callable.object_ptr = NULL;

	zend_try {
		PHP_THREAD_SELF = entry;

		php_thread_compiler_globals_reinit(
				(zend_compiler_globals*)(*tsrm_ls)[TSRM_UNSHUFFLE_RSRC_ID(compiler_globals_id)],
				(zend_compiler_globals*)(*entry->parent->tsrm_ls)[TSRM_UNSHUFFLE_RSRC_ID(compiler_globals_id)]);
		php_thread_executor_globals_reinit(
				(zend_executor_globals*)(*tsrm_ls)[TSRM_UNSHUFFLE_RSRC_ID(executor_globals_id)],
				(zend_executor_globals*)(*entry->parent->tsrm_ls)[TSRM_UNSHUFFLE_RSRC_ID(executor_globals_id)]);

		callable.params = safe_emalloc(param->callable.param_count, sizeof(zval**), 0);
		args = safe_emalloc(param->callable.param_count, sizeof(zval*), 0);
		for (i = 0; i < param->callable.param_count; ++i) {
			if (FAILURE == php_thread_convert_object_ref(&args[i],
					*param->callable.params[i],
					entry->parent->tsrm_ls TSRMLS_CC)) {
				zend_bailout();
			}
			callable.params[i] = &args[i];
		}

		if (param->callable.object_ptr) {
			if (FAILURE == php_thread_convert_object_ref(
					&callable.object_ptr,
					param->callable.object_ptr,
					entry->parent->tsrm_ls TSRMLS_CC)) {
				zend_bailout();
			}
		}

		if (FAILURE == php_thread_convert_object_ref(
				&callable.function_name,
				param->callable.function_name,
				entry->parent->tsrm_ls TSRMLS_CC)) {
			zend_bailout();
		}

		param->status = 0; /* no error */
	} zend_end_try();

	if (pthread_mutex_lock(&param->ready_cond_mtx) != 0) {
		goto out;
	}
	if (pthread_cond_signal(&param->ready_cond) != 0) {
		bail_if_fail(pthread_mutex_unlock(&param->ready_cond_mtx));
		goto out;
	}
	bail_if_fail(pthread_mutex_unlock(&param->ready_cond_mtx));

	if (param->status) {
		goto out;
	}

	zend_try {
		ALLOC_INIT_ZVAL(retval);
		callable.retval_ptr_ptr = &retval;
		if (FAILURE == zend_call_function(&callable, NULL TSRMLS_CC)) {
			zval_ptr_dtor(&retval);
			retval = NULL;
		} else {
			zval *new_retval;
			if (FAILURE == php_thread_convert_object_ref(&new_retval,
					retval, tsrm_ls, entry->parent->tsrm_ls)) {
				zval_ptr_dtor(&retval);
				retval = NULL;
			} else {
				retval = new_retval;
			}
		}
	} zend_end_try();

	php_thread_entry_wait(entry TSRMLS_CC);

out:
	for (i = 0; i < callable.param_count; ++i) {
		zval_ptr_dtor(&args[i]);
	}

	if (callable.function_name) {
		zval_ptr_dtor(&callable.function_name);
	}

	if (callable.object_ptr) {
		zval_ptr_dtor(&callable.object_ptr);
	}

	efree(callable.params);
	efree(args);

	php_request_shutdown(NULL);

	entry->finished = 1;
	--entry->parent->alive_subthread_count;

	php_thread_entry_dispose(&entry TSRMLS_CC);

	return (void*)retval;
}
/* }}} */

/* {{{ proto resource thread_create(callable entry_function, ...)
   Creates a new thread and returns the thread handle */
PHP_FUNCTION(thread_create)
{
	zval ***args = NULL;
	char *callable_str_repr = NULL;
	php_thread_thread_param_t param;
	zend_fcall_info_cache fcc;
	int nargs;

	{
		nargs = ZEND_NUM_ARGS();
		if (nargs < 1) {
			ZEND_WRONG_PARAM_COUNT();
			return;
		}
		args = (zval ***)safe_emalloc(nargs, sizeof(zval *), 0);
		if (FAILURE == zend_get_parameters_array_ex(nargs, args)) {
			ZEND_WRONG_PARAM_COUNT();
			RETVAL_FALSE;
			if (args) {
				efree(args);
			}
		}
	}

	if (zend_fcall_info_init(*args[0], 0, &param.callable, &fcc,
				NULL, &callable_str_repr TSRMLS_CC) != SUCCESS) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "The argument (%s) is not callable", callable_str_repr);
		efree(callable_str_repr);
		RETVAL_FALSE;
		if (args) {
			efree(args);
		}
	} else {
		if (callable_str_repr) {
			php_error_docref(NULL TSRMLS_CC, E_STRICT, "The argument (%s) is not callable", callable_str_repr);
			efree(callable_str_repr);
		} else {
			efree(callable_str_repr);
		}
	}

	{
		php_thread_entry_t *current_entry = PHP_THREAD_SELF;
		if (pthread_mutex_init(&param.ready_cond_mtx, NULL) != 0) {
			RETVAL_FALSE;
			if (args) {
				efree(args);
			}
		}
		if (pthread_cond_init(&param.ready_cond, NULL) != 0) {
			RETVAL_FALSE;
			if (args) {
				efree(args);
			}
		}
		param.compiler_globals = (zend_compiler_globals*)(*tsrm_ls)[TSRM_UNSHUFFLE_RSRC_ID(compiler_globals_id)];
		param.executor_globals = (zend_executor_globals*)(*tsrm_ls)[TSRM_UNSHUFFLE_RSRC_ID(executor_globals_id)];
		param.callable.param_count = nargs - 1;
		param.callable.params = args + 1;
		param.status = -1;
		param.entry = pemalloc(sizeof(*param.entry), 1);
		if (!param.entry) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Insufficient memory");
			RETVAL_FALSE;
			if (args) {
				efree(args);
			}
			return;
		}

		if (current_entry->subthreads.cap <= current_entry->subthreads.n) {
			size_t new_cap = current_entry->subthreads.cap == 0 ?
					1: current_entry->subthreads.cap * 2;
			php_thread_entry_t **new_list = NULL;
			if (new_cap / 2 != current_entry->subthreads.cap) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Insufficient memory");
				RETVAL_FALSE;
				if (args) {
					efree(args);
				}
				return;
			}
			new_list = safe_perealloc(current_entry->subthreads.v,
					new_cap, sizeof(*new_list), 0, 1);
			if (!new_list) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Insufficient memory");
				RETVAL_FALSE;
				if (args) {
					efree(args);
				}
				return;
			}
			current_entry->subthreads.cap = new_cap;
			current_entry->subthreads.v = new_list;
		}

		if (FAILURE == php_thread_entry_ctor(param.entry, current_entry,
				current_entry->subthreads.n)) {
			pefree(param.entry, 1);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to spawn a new thread from php_thread_entry_ctor");
			RETVAL_FALSE;
			if (args) {
				efree(args);
			}
			return;
		}
		++current_entry->subthreads.n;
		


		// returns 0 for success. NULL is default attrs, 
		if (pthread_create(&param.entry->t, NULL, (void*(*)(void*))_php_thread_entry_func, &param) != 0) {
			--current_entry->subthreads.n;
			pefree(param.entry, 1);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to spawn a new thread from pthread_create");
			RETVAL_FALSE;
			if (args) {
				efree(args);
			}
			return;
		}

		bail_if_fail(pthread_mutex_lock(&param.ready_cond_mtx));
		bail_if_fail(pthread_cond_wait(&param.ready_cond, &param.ready_cond_mtx));
		bail_if_fail(pthread_mutex_unlock(&param.ready_cond_mtx));

		if (param.status) {
			current_entry->subthreads.n--;
			pefree(param.entry, 1);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to spawn a new thread from param.status");
			RETVAL_FALSE;
			if (args) {
				efree(args);
			}
			return;
		}

		current_entry->subthreads.v[param.entry->serial] = param.entry;

		ZEND_REGISTER_RESOURCE(return_value, param.entry, global_ctx.le_thread);
	}
}
/* }}} */

/* {{{ proto void thread_suspend(resource thread)
   Suspends the specified thread */
PHP_FUNCTION(thread_suspend)
{
	zval *zv;
	php_thread_entry_t *entry;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zv)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(entry, php_thread_entry_t *, &zv, -1, "thread handle",
			global_ctx.le_thread);

	php_thread_entry_suspend(entry);
}
/* }}} */

/* {{{ proto void thread_resume(resource thread)
   Resumes a suspended thread */
PHP_FUNCTION(thread_resume)
{
	zval *zv;
	php_thread_entry_t *entry;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zv)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(entry, php_thread_entry_t *, &zv, -1, "thread handle",
			global_ctx.le_thread);

	php_thread_entry_resume(entry);
}
/* }}} */

/* {{{ proto mixed thread_join(resource thread)
   Wait for a thread to complete */
PHP_FUNCTION(thread_join)
{
	zval *zv;
	php_thread_entry_t *entry;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zv)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(entry, php_thread_entry_t *, &zv, -1, "thread handle",
			global_ctx.le_thread);

	{
		zval *tmp;

		THR_DEBUG(("calling php_thread_entry_join in thread_join\n"));
		php_thread_entry_join(entry, &tmp TSRMLS_CC);
		/*if (tmp != NULL) {
			THR_DEBUG(("returning value (not null)\n"));
			*return_value = *tmp;
			FREE_ZVAL(tmp);
		} else {*/
			THR_DEBUG(("returning null\n"));
			RETURN_TRUE;
		//}
	}
}
/* }}} */

/* {{{ proto resource thread_mutex_create()
   Creates a mutex */
PHP_FUNCTION(thread_mutex_create)
{
	php_thread_mutex_t *mtx = pemalloc(sizeof(*mtx), 1);
	if (!mtx) {
		RETURN_FALSE;
	}
	if (FAILURE == php_thread_mutex_ctor(mtx)) {
		pefree(mtx, 1);
		RETURN_FALSE;
	}
	ZEND_REGISTER_RESOURCE(return_value, mtx, global_ctx.le_mutex);
}
/* }}} */

/* {{{ proto mixed thread_mutex_acquire(resource mutex [, float timeout])
   Acquires a mutex lock ownership */
PHP_FUNCTION(thread_mutex_acquire)
{
	zval *zv;
	double timeout = -1.0;
	php_thread_mutex_t *mtx;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|d", &zv, &timeout)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(mtx, php_thread_mutex_t *, &zv, -1, "thread mutex",
			global_ctx.le_mutex);

	switch (php_thread_mutex_acquire(mtx, timeout TSRMLS_CC)) {
	case PHP_THREAD_LOCK_FAILED:
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to acquire a mutex ownership");
		RETURN_FALSE;
	case PHP_THREAD_LOCK_ACQUIRED:
		RETURN_TRUE;
	case PHP_THREAD_LOCK_TIMEOUT:
		RETURN_LONG(0);
	}
}
/* }}} */

/* {{{ proto bool thread_mutex_release(resource mutex)
   Releases a mutex lock ownership */
PHP_FUNCTION(thread_mutex_release)
{
	zval *zv;
	php_thread_mutex_t *mtx;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zv)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(mtx, php_thread_mutex_t *, &zv, -1, "thread mutex",
			global_ctx.le_mutex);

	if (FAILURE == php_thread_mutex_release(mtx TSRMLS_CC)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to release a mutex; possibly it is not owned by the current thread");
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto mixed thread_message_queue_create()
   Create a message queue */
PHP_FUNCTION(thread_message_queue_create)
{
	php_thread_message_queue_t *queue = pemalloc(sizeof(*queue), 1);
	if (!queue) {
		RETURN_FALSE;
	}
	if (FAILURE == php_thread_message_queue_ctor(queue)) {
		pefree(queue, 1);
		RETURN_FALSE;
	}
	ZEND_REGISTER_RESOURCE(return_value, queue, global_ctx.le_msg_queue);
}
/* }}} */

/* {{{ proto mixed thread_message_queue_post(resource queue, mixed message)
   Post a message queue */
PHP_FUNCTION(thread_message_queue_post)
{
	//int retval;
	zval *zv, *msg;
	php_thread_message_queue_t *queue;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rz", &zv, &msg)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(queue, php_thread_message_queue_t*, &zv, -1, "thread message queue",
			global_ctx.le_msg_queue);

	RETVAL_BOOL(SUCCESS == php_thread_message_queue_post(queue, msg TSRMLS_CC));
}
/* }}} */

/* {{{ proto mixed thread_message_queue_poll(resource queue)
   Poll on a message queue */
PHP_FUNCTION(thread_message_queue_poll)
{
	//int retval;
	zval *zv;
	php_thread_message_queue_t *queue;
	zval *tmp;

	THR_DEBUG(("php_thread_do_exit: %d\n", php_thread_do_exit));
	if(php_thread_do_exit == 1) {
		RETURN_STRING("PHP_THREAD_POLL_STOP", FALSE);
	}

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zv)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(queue, php_thread_message_queue_t*, &zv, -1, "thread message queue",
			global_ctx.le_msg_queue);

	if (FAILURE == php_thread_message_queue_poll(queue, &tmp TSRMLS_CC)) {
		RETURN_NULL();
	} else {
		*return_value = *tmp;
		FREE_ZVAL(tmp);
	}
}
/* }}} */

/* {{{ proto bool thread_message_queue_poll(resource queue)
   Stop waiting on never coming queue */
PHP_FUNCTION(thread_message_queue_stop)
{
	//int retval;
	zval *zv;
	php_thread_message_queue_t *queue;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zv)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(queue, php_thread_message_queue_t*, &zv, -1, "thread message queue",
			global_ctx.le_msg_queue);

	THR_DEBUG(("--attempting to stop signal from thread_message_queue_stop\n"));
	if (FAILURE == php_thread_message_queue_stop(queue TSRMLS_CC)) {
		THR_DEBUG(("--returning\n"));
		RETURN_FALSE;
	} else {
		THR_DEBUG(("--returning\n"));
		RETURN_TRUE;
	}
}
/* }}} */

/* {{{ proto mixed thread_message_slot_create()
   Create a message slot */
PHP_FUNCTION(thread_message_slot_create)
{
	php_thread_message_slot_t *slot = pemalloc(sizeof(*slot), 1);
	if (!slot) {
		RETURN_FALSE;
	}
	if (FAILURE == php_thread_message_slot_ctor(slot)) {
		pefree(slot, 1);
		RETURN_FALSE;
	}
	ZEND_REGISTER_RESOURCE(return_value, slot, global_ctx.le_msg_slot);
}
/* }}} */

/* {{{ proto mixed thread_message_slot_post(resource slot, mixed message [, bool broadcast])
   Post a message slot */
PHP_FUNCTION(thread_message_slot_post)
{
	//int retval;
	zval *zv, *msg;
	php_thread_message_slot_t *slot;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rz", &zv, &msg)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(slot, php_thread_message_slot_t*, &zv, -1, "thread message slot",
			global_ctx.le_msg_slot);

	RETURN_BOOL(SUCCESS == php_thread_message_slot_post(slot, msg TSRMLS_CC));
}
/* }}} */

/* {{{ proto mixed thread_message_slot_subscribe(resource slot)
   Poll on a message slot */
PHP_FUNCTION(thread_message_slot_subscribe)
{
	//int retval;
	zval *zv;
	php_thread_message_slot_t *slot;
	zval *tmp;

	if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zv)) {
		RETURN_FALSE;
	}

	ZEND_FETCH_RESOURCE(slot, php_thread_message_slot_t*, &zv, -1, "thread message slot",
			global_ctx.le_msg_slot);

	if (FAILURE == php_thread_message_slot_subscribe(slot, &tmp TSRMLS_CC)) {
		RETURN_NULL();
	} else {
		*return_value = *tmp;
		FREE_ZVAL(tmp);
	}
}
/* }}} */

static void _php_thread_free_thread_entry(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	php_thread_entry_dispose((php_thread_entry_t **)&rsrc->ptr TSRMLS_CC);
}

static void _php_thread_free_mutex_entry(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	php_thread_mutex_dispose((php_thread_mutex_t **)&rsrc->ptr TSRMLS_CC);
}

static void _php_thread_free_message_queue_entry(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	php_thread_message_queue_dispose((php_thread_message_queue_t **)&rsrc->ptr TSRMLS_CC);
}

static void _php_thread_free_message_slot_entry(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	php_thread_message_slot_dispose((php_thread_message_slot_t **)&rsrc->ptr TSRMLS_CC);
}

static void _php_thread_signal_handler(int sig, void *si, void *ctx)
{
	TSRMLS_FETCH();
	php_thread_entry_t *self = PHP_THREAD_SELF;
	php_thread_message_t *msg, *prev;
	bail_if_fail(pthread_mutex_lock(&self->gc_mtx));
	for (msg = self->garbage; msg; msg = prev) {
		prev = msg->prev;
		zval_ptr_dtor(&msg->value);
		pefree(msg, 1);
	}
	bail_if_fail(pthread_mutex_unlock(&self->gc_mtx));
	self->garbage = NULL;
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(threading)
{
	php_thread_entry_ctor(&global_ctx.entry, NULL, 0);
	global_ctx.entry.t = pthread_self();
	global_ctx.entry.tsrm_ls = tsrm_ls;
	ts_allocate_id(&global_ctx.tsrm_id,
			sizeof(php_thread_entry_t*), NULL, NULL);
	global_ctx.le_thread = zend_register_list_destructors_ex(
			(rsrc_dtor_func_t)_php_thread_free_thread_entry,
			NULL, "thread handle", module_number);
	global_ctx.le_mutex = zend_register_list_destructors_ex(
			(rsrc_dtor_func_t)_php_thread_free_mutex_entry,
			NULL, "thread mutex", module_number);
	global_ctx.le_msg_queue = zend_register_list_destructors_ex(
			(rsrc_dtor_func_t)_php_thread_free_message_queue_entry,
			NULL, "thread message queue", module_number);
	global_ctx.le_msg_slot = zend_register_list_destructors_ex(
			(rsrc_dtor_func_t)_php_thread_free_message_queue_entry,
			NULL, "thread message slot", module_number);
	PHP_THREAD_SELF = &global_ctx.entry;
#ifndef PHP_WIN32
	{
		struct sigaction sa;
		sa.sa_sigaction = _php_thread_signal_handler;
		sa.sa_flags = SA_SIGINFO;
		sigfillset(&sa.sa_mask);
		if (sigaction(31, &sa, NULL)) {
			return FAILURE;
		}
	}
#endif
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(threading)
{
	php_thread_entry_dtor(&global_ctx.entry TSRMLS_CC);
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(threading)
{
	php_thread_entry_t *entry = PHP_THREAD_SELF;
	if (entry != &global_ctx.entry) {
		return SUCCESS;
	}
	php_thread_entry_wait(entry TSRMLS_CC);
	entry->finished = 1;
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(threading)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "threading support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
