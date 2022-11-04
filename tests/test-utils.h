/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include "libsoup/soup.h"

void test_init    (int argc, char **argv, GOptionEntry *entries);
void test_cleanup (void);

extern int debug_level;
extern gboolean tls_available;
extern gboolean apache_available;
void debug_printf (int level, const char *format, ...) G_GNUC_PRINTF (2, 3);

#define SOUP_TEST_SKIP_IF_NO_TLS				\
	G_STMT_START {						\
		if (!tls_available) {				\
			g_test_skip ("TLS is not available");	\
			return;					\
		}						\
	} G_STMT_END

#define SOUP_TEST_SKIP_IF_NO_IPV6                               \
        G_STMT_START {                                          \
                if (g_getenv ("SOUP_TEST_NO_IPV6")) {           \
                        g_test_skip ("IPV6 is not available");  \
                        return;                                 \
                }                                               \
        } G_STMT_END                                            \

#ifdef HAVE_APACHE
void apache_init    (void);
void apache_cleanup (void);
#define SOUP_TEST_SKIP_IF_NO_APACHE
#else
#define apache_init()
#define apache_cleanup()
#define SOUP_TEST_SKIP_IF_NO_APACHE				\
	G_STMT_START {						\
		g_test_skip ("apache is not available");	\
		return;						\
	} G_STMT_END
#endif

gboolean have_curl (void);

typedef enum {
	SOUP_TEST_REQUEST_NONE = 0,
	SOUP_TEST_REQUEST_CANCEL_SOON = (1 << 0),
	SOUP_TEST_REQUEST_CANCEL_IMMEDIATE = (1 << 1),
	SOUP_TEST_REQUEST_CANCEL_PREEMPTIVE = (1 << 2),
	SOUP_TEST_REQUEST_CANCEL_AFTER_SEND_FINISH = (1 << 3),
        SOUP_TEST_REQUEST_CANCEL_BY_SESSION = (1 << 4)
} SoupTestRequestFlags;

SoupSession *soup_test_session_new                (const char   *propname, ...);
void         soup_test_session_abort_unref        (SoupSession  *session);
GBytes      *soup_test_session_async_send         (SoupSession  *session,
						   SoupMessage  *msg,
						   GCancellable *cancellable,
						   GError      **error);
guint        soup_test_session_send_message       (SoupSession  *session,
						   SoupMessage  *msg);

typedef enum {
	SOUP_TEST_SERVER_DEFAULT             = 0,
	SOUP_TEST_SERVER_IN_THREAD           = (1 << 0),
	SOUP_TEST_SERVER_NO_DEFAULT_LISTENER = (1 << 1),
	SOUP_TEST_SERVER_UNIX_SOCKET         = (1 << 2),
        SOUP_TEST_SERVER_HTTP2               = (1 << 3)
} SoupTestServerOptions;

SoupServer  *soup_test_server_new            (SoupTestServerOptions  options);
const char  *soup_test_server_get_unix_path  (SoupServer            *server);
void         soup_test_server_run_in_thread  (SoupServer            *server);
GUri        *soup_test_server_get_uri        (SoupServer            *server,
					      const char            *scheme,
					      const char            *host);
void         soup_test_server_quit_unref     (SoupServer            *server);

GInputStream *soup_test_request_send         (SoupSession  *session,
					      SoupMessage  *msg,
					      GCancellable *cancellable,
					      guint         flags,
					      GError       **error);
gboolean      soup_test_request_read_all     (GInputStream  *stream,
					      GCancellable  *cancellable,
					      GError       **error);
gboolean      soup_test_request_close_stream (GInputStream  *stream,
					      GCancellable  *cancellable,
					      GError       **error);

void        soup_test_register_resources (void);
GBytes     *soup_test_load_resource      (const char  *name,
					  GError     **error);

GBytes     *soup_test_get_index          (void);

char       *soup_test_build_filename_abs (GTestFileType  file_type,
                                          const gchar   *first_path,
                                          ...);


#ifdef G_HAVE_ISO_VARARGS
#define soup_test_assert(expr, ...)				\
G_STMT_START {								\
	char *_message;							\
	if (G_UNLIKELY (!(expr))) {					\
		_message = g_strdup_printf (__VA_ARGS__);		\
		g_assertion_message (G_LOG_DOMAIN,			\
				     __FILE__, __LINE__, G_STRFUNC,	\
				     _message);				\
		g_free (_message);					\
	}								\
} G_STMT_END
#else
void soup_test_assert (gboolean expr, const char *fmt, ...);
#endif

#define soup_test_assert_message_status(msg, status)			\
G_STMT_START {								\
	SoupMessage *_msg = (msg);					\
	guint _status = (status);					\
	char *_message;							\
									\
	if (G_UNLIKELY (soup_message_get_status (_msg) != _status)) {		\
		_message = g_strdup_printf ("Unexpected status %d %s (expected %d %s)", \
					    soup_message_get_status (_msg), soup_message_get_reason_phrase (_msg),     \
					    _status, soup_status_get_phrase (_status)); \
		g_assertion_message (G_LOG_DOMAIN,			\
				     __FILE__, __LINE__, G_STRFUNC,	\
				     _message);				\
		g_free (_message);					\
	}								\
} G_STMT_END

#define soup_test_assert_handled_by(msg, string)			\
G_STMT_START {								\
	SoupMessage *_msg = (msg);					\
	const char *_handled_by;					\
	const char *_expected = (char *) (string);			\
	char *_message;							\
	_handled_by = soup_message_headers_get_one (soup_message_get_response_headers (_msg), \
	                                            "X-Handled-By"); 	\
	if (G_UNLIKELY (g_strcmp0 (_handled_by, _expected) != 0)) {	\
		_message = g_strdup_printf ("Request was handled by %s (expected %s)", \
		                            _handled_by, _expected);	\
		g_assertion_message (G_LOG_DOMAIN,			\
		                     __FILE__, __LINE__, G_STRFUNC,	\
		                     _message);				\
		g_free (_message);					\
	}								\
} G_STMT_END

#define soup_assert_cmpmem(s1, l1, s2, l2)				\
G_STMT_START {								\
	int __l1 = l1, __l2 = l2;					\
	gconstpointer __s1 = s1, __s2 = s2;				\
	if (G_UNLIKELY ((__l1) != (__l2))) {				\
		g_assertion_message_cmpnum (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
					    "len(" #s1 ") == len(" #s2 ")", __l1, "==", __l2, \
					    'i');			\
	} else if (G_UNLIKELY (memcmp (__s1, __s2, __l1) != 0)) {	\
		g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
				     "assertion failed (" #s1 " == " #s2 ")"); \
	}								\
} G_STMT_END
