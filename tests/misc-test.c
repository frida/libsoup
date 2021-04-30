/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright 2007-2012 Red Hat, Inc.
 */

#include "test-utils.h"
#include "soup-connection.h"
#include "soup-session-private.h"

SoupServer *server;
GUri *base_uri;

static gboolean
auth_callback (SoupAuthDomain *auth_domain, SoupMessage *msg,
	       const char *username, const char *password, gpointer data)
{
	return !strcmp (username, "user") && !strcmp (password, "password");
}

static gboolean
timeout_finish_message (gpointer msg)
{
	SoupServer *server = g_object_get_data (G_OBJECT (msg), "server");

	soup_server_unpause_message (server, msg);
	return FALSE;
}

static void
server_callback (SoupServer        *server,
		 SoupServerMessage *msg,
		 const char        *path,
		 GHashTable        *query,
		 gpointer           data)
{
	const char *method = soup_server_message_get_method (msg);
	GUri *uri = soup_server_message_get_uri (msg);

	if (method != SOUP_METHOD_GET && method != SOUP_METHOD_POST) {
		soup_server_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED, NULL);
		return;
	}

	if (!strcmp (path, "/redirect")) {
		soup_server_message_set_redirect (msg, SOUP_STATUS_FOUND, "/");
		return;
	}

	if (!strcmp (path, "/slow")) {
                GSource *timeout;
		soup_server_pause_message (server, msg);
		g_object_set_data (G_OBJECT (msg), "server", server);
		timeout = soup_add_timeout (g_main_context_get_thread_default (),
                                            1000, timeout_finish_message, msg);
                g_source_unref (timeout);
	}

	soup_server_message_set_status (msg, SOUP_STATUS_OK, NULL);
	if (!strcmp (g_uri_get_host (uri), "foo")) {
		soup_server_message_set_response (msg, "text/plain",
                                                  SOUP_MEMORY_STATIC, "foo-index", 9);
		return;
	} else {
		soup_server_message_set_response (msg, "text/plain",
						  SOUP_MEMORY_STATIC, "index", 5);
		return;
	}
}

/* Host header handling: client must be able to override the default
 * value, server must be able to recognize different Host values.
 */
static void
do_host_test (void)
{
	SoupSession *session;
	SoupMessage *one, *two;
	GBytes *body_one, *body_two;

	g_test_bug ("539803");

	session = soup_test_session_new (NULL);

	one = soup_message_new_from_uri ("GET", base_uri);
	two = soup_message_new_from_uri ("GET", base_uri);
	soup_message_headers_replace (soup_message_get_request_headers (two), "Host", "foo");

	body_one = soup_session_send_and_read (session, one, NULL, NULL);
	body_two = soup_session_send_and_read (session, two, NULL, NULL);

	soup_test_session_abort_unref (session);

	soup_test_assert_message_status (one, SOUP_STATUS_OK);
	g_assert_cmpstr (g_bytes_get_data (body_one, NULL), ==, "index");
	g_bytes_unref (body_one);
	g_object_unref (one);

	soup_test_assert_message_status (two, SOUP_STATUS_OK);
	g_assert_cmpstr (g_bytes_get_data (body_two, NULL), ==, "foo-index");
	g_bytes_unref (body_two);
	g_object_unref (two);
}

/* request with too big header should be discarded with a IO error to 
 * prevent DOS attacks.
 */
static void
do_host_big_header (void)
{
	SoupMessage *msg;
	SoupSession *session;
	int i;
	GInputStream *stream;
	GError *error = NULL;

	g_test_bug ("792173");

	session = soup_test_session_new (NULL);

	msg = soup_message_new_from_uri ("GET", base_uri);
	for (i = 0; i < 2048; i++) {
		char *key = g_strdup_printf ("test-long-header-key%d", i);
		char *value = g_strdup_printf ("test-long-header-key%d", i);
		soup_message_headers_append (soup_message_get_request_headers (msg), key, value);
		g_free (value);
		g_free (key);
	}

	stream = soup_session_send (session, msg, NULL, &error);
	g_assert_null (stream);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);

	soup_test_session_abort_unref (session);

	g_object_unref (msg);
}

/* Dropping the application's ref on the session from a callback
 * should not cause the session to be freed at an incorrect time.
 * (This test will crash if it fails.)
 */
static void
cu_one_completed (SoupMessage *msg,
		  SoupSession *session)
{
	debug_printf (2, "  Message 1 completed\n");
	g_object_unref (session);
}

static gboolean
cu_idle_quit (gpointer loop)
{
	g_main_loop_quit (loop);
	return FALSE;
}

static void
cu_two_completed (SoupMessage *msg,
		  GMainLoop   *loop)
{
	debug_printf (2, "  Message 2 completed\n");
	g_idle_add (cu_idle_quit, loop); 
}

static void
do_callback_unref_test (void)
{
	SoupServer *bad_server;
	SoupSession *session;
	SoupMessage *one, *two;
	GMainLoop *loop;
	GUri *bad_uri;

	g_test_bug ("533473");

	/* Get a guaranteed-bad URI */
	bad_server = soup_test_server_new (SOUP_TEST_SERVER_DEFAULT);
	bad_uri = soup_test_server_get_uri (bad_server, "http", NULL);
	soup_test_server_quit_unref (bad_server);

	session = soup_test_session_new (NULL);
	g_object_add_weak_pointer (G_OBJECT (session), (gpointer *)&session);

	loop = g_main_loop_new (NULL, TRUE);

	one = soup_message_new_from_uri ("GET", bad_uri);
	g_signal_connect (one, "finished",
			  G_CALLBACK (cu_one_completed), session);
	g_object_add_weak_pointer (G_OBJECT (one), (gpointer *)&one);
	two = soup_message_new_from_uri ("GET", bad_uri);
	g_signal_connect (two, "finished",
			  G_CALLBACK (cu_two_completed), loop);
	g_object_add_weak_pointer (G_OBJECT (two), (gpointer *)&two);
	g_uri_unref (bad_uri);

	soup_session_send_async (session, one, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
	soup_session_send_async (session, two, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
	g_object_unref (one);
	g_object_unref (two);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_assert_null (session);
	if (session) {
		g_object_remove_weak_pointer (G_OBJECT (session), (gpointer *)&session);
		g_object_unref (session);
	}
	g_assert_null (one);
	if (one) {
		g_object_remove_weak_pointer (G_OBJECT (one), (gpointer *)&one);
		g_object_unref (one);
	}
	g_assert_null (two);
	if (two) {
		g_object_remove_weak_pointer (G_OBJECT (two), (gpointer *)&two);
		g_object_unref (two);
	}

	/* Otherwise, if we haven't crashed, we're ok. */
}

/* SoupSession should clean up all internal signal handlers on a message after
 * it is finished, allowing the message to be reused if desired.
 */
static void
ensure_no_signal_handlers (SoupMessage *msg, guint *signal_ids, guint n_signal_ids)
{
	int i;
	guint id;

	for (i = 0; i < n_signal_ids; i++) {
		if (strcmp (g_signal_name (signal_ids[i]), "authenticate") == 0)
			continue;

		id = g_signal_handler_find (msg, G_SIGNAL_MATCH_ID, signal_ids[i],
					    0, NULL, NULL, NULL);
		soup_test_assert (id == 0,
				  "message has handler for '%s'",
				  g_signal_name (signal_ids[i]));
	}
}

static gboolean
reuse_test_authenticate (SoupMessage *msg,
			 SoupAuth    *auth,
			 gboolean     retrying)
{
	/* Get it wrong the first time, then succeed */
	if (!retrying)
		soup_auth_authenticate (auth, "user", "wrong password");
	else
		soup_auth_authenticate (auth, "user", "password");

	return TRUE;
}

static void
do_msg_reuse_test (void)
{
	SoupSession *session;
	SoupMessage *msg;
	GUri *uri;
	guint *signal_ids, n_signal_ids;

	g_test_bug ("559054");

	signal_ids = g_signal_list_ids (SOUP_TYPE_MESSAGE, &n_signal_ids);

	session = soup_test_session_new (NULL);

	debug_printf (1, "  First message\n");
	msg = soup_message_new_from_uri ("GET", base_uri);
	g_signal_connect (msg, "authenticate",
                          G_CALLBACK (reuse_test_authenticate), NULL);
	soup_test_session_async_send (session, msg, NULL, NULL);
	ensure_no_signal_handlers (msg, signal_ids, n_signal_ids);

	debug_printf (1, "  Redirect message\n");
	uri = g_uri_parse_relative (base_uri, "/redirect", SOUP_HTTP_URI_FLAGS, NULL);
	soup_message_set_uri (msg, uri);
	g_uri_unref (uri);
	soup_test_session_async_send (session, msg, NULL, NULL);
	g_assert_true (soup_uri_equal (soup_message_get_uri (msg), base_uri));
	ensure_no_signal_handlers (msg, signal_ids, n_signal_ids);

	debug_printf (1, "  Auth message\n");
	uri = g_uri_parse_relative (base_uri, "/auth", SOUP_HTTP_URI_FLAGS, NULL);
	soup_message_set_uri (msg, uri);
	g_uri_unref (uri);
	soup_test_session_async_send (session, msg, NULL, NULL);
	soup_test_assert_message_status (msg, SOUP_STATUS_OK);
	soup_message_set_uri (msg, base_uri);
	soup_test_session_async_send (session, msg, NULL, NULL);
	ensure_no_signal_handlers (msg, signal_ids, n_signal_ids);

	soup_test_session_abort_unref (session);
	g_object_unref (msg);
	g_free (signal_ids);
}

/* Handle unexpectedly-early aborts. */
static void
ea_msg_completed_one (SoupSession  *session,
		      GAsyncResult *result,
		      GMainLoop    *loop)
{
	GError *error = NULL;

	debug_printf (2, "  Message 1 completed\n");
	g_assert_null (soup_session_send_finish (session, result, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error (&error);
	g_main_loop_quit (loop);
}

static gboolean
ea_abort_session (gpointer session)
{
	soup_session_abort (session);
	return FALSE;
}

static void
ea_message_network_event (SoupMessage       *msg,
			  GSocketClientEvent event,
			  GIOStream         *connection,
			  SoupSession       *session)
{
	if (event != G_SOCKET_CLIENT_RESOLVING)
		return;

	g_idle_add_full (G_PRIORITY_HIGH,
			 ea_abort_session,
			 session, NULL);
	g_signal_handlers_disconnect_by_func (msg, ea_message_network_event, session);
}

static void
ea_message_starting (SoupMessage  *msg,
		     GCancellable *cancellable)
{
	g_cancellable_cancel (cancellable);
}

static void
do_early_abort_test (void)
{
	SoupSession *session;
	SoupMessage *msg;
	GCancellable *cancellable;
	GMainContext *context;
	GMainLoop *loop;
	GError *error = NULL;

	g_test_bug ("596074");
	g_test_bug ("618641");

	session = soup_test_session_new (NULL);
	msg = soup_message_new_from_uri ("GET", base_uri);

	context = g_main_context_default ();
	loop = g_main_loop_new (context, TRUE);
	soup_session_send_async (session, msg, G_PRIORITY_DEFAULT, NULL,
				 (GAsyncReadyCallback)ea_msg_completed_one,
				 loop);
	g_object_unref (msg);
	g_main_context_iteration (context, FALSE);

	soup_session_abort (session);
	while (g_main_context_pending (context))
		g_main_context_iteration (context, FALSE);
	g_main_loop_unref (loop);
	soup_test_session_abort_unref (session);

	session = soup_test_session_new (NULL);
	msg = soup_message_new_from_uri ("GET", base_uri);

	g_signal_connect (msg, "network-event",
			  G_CALLBACK (ea_message_network_event),
			  session);
	g_assert_null (soup_test_session_async_send (session, msg, NULL, &error));
	debug_printf (2, "  Message 2 completed\n");

	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error (&error);
	g_object_unref (msg);

	while (g_main_context_pending (context))
		g_main_context_iteration (context, FALSE);

	soup_test_session_abort_unref (session);

	g_test_bug ("668098");

	session = soup_test_session_new (NULL);
	msg = soup_message_new_from_uri ("GET", base_uri);
	cancellable = g_cancellable_new ();

	g_signal_connect (msg, "starting",
			  G_CALLBACK (ea_message_starting), cancellable);
	g_assert_null (soup_test_session_async_send (session, msg, cancellable, &error));
	debug_printf (2, "  Message 3 completed\n");

	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error (&error);
	g_object_unref (cancellable);
	g_object_unref (msg);

	while (g_main_context_pending (context))
		g_main_context_iteration (context, FALSE);

	soup_test_session_abort_unref (session);
}

static void
do_one_accept_language_test (const char *language, const char *expected_header)
{
	SoupSession *session;
	SoupMessage *msg;
	const char *val;

	debug_printf (1, "  LANGUAGE=%s\n", language);
	g_setenv ("LANGUAGE", language, TRUE);
	session = soup_test_session_new ("accept-language-auto", TRUE,
					 NULL);
	msg = soup_message_new_from_uri ("GET", base_uri);
	soup_test_session_send_message (session, msg);
	soup_test_session_abort_unref (session);

	soup_test_assert_message_status (msg, SOUP_STATUS_OK);
	val = soup_message_headers_get_list (soup_message_get_request_headers (msg),
					     "Accept-Language");
	g_assert_cmpstr (val, ==, expected_header);

	g_object_unref (msg);
}

static void
do_accept_language_test (void)
{
	const char *orig_language;

	g_test_bug ("602547");

	orig_language = g_getenv ("LANGUAGE");
	do_one_accept_language_test ("C", "en");
	do_one_accept_language_test ("fr_FR", "fr-fr, fr;q=0.9");
	do_one_accept_language_test ("fr_FR:de:en_US", "fr-fr, fr;q=0.9, de;q=0.8, en-us;q=0.7, en;q=0.6");

	if (orig_language)
		g_setenv ("LANGUAGE", orig_language, TRUE);
	else
		g_unsetenv ("LANGUAGE");
}

static gboolean
cancel_message_timeout (GCancellable *cancellable)
{
	g_cancellable_cancel (cancellable);
	return FALSE;
}

static void
do_cancel_while_reading_test_for_session (SoupSession *session)
{
	SoupMessage *msg;
	GUri *uri;
	GCancellable *cancellable;
	GError *error = NULL;

	uri = g_uri_parse_relative (base_uri, "/slow", SOUP_HTTP_URI_FLAGS, NULL);
	msg = soup_message_new_from_uri ("GET", uri);
	g_uri_unref (uri);

	cancellable = g_cancellable_new ();
	g_timeout_add_full (G_PRIORITY_DEFAULT, 100,
			    (GSourceFunc)cancel_message_timeout,
			    g_object_ref (cancellable),
			    g_object_unref);

	g_assert_null (soup_test_session_async_send (session, msg, cancellable, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error (&error);
	g_object_unref (cancellable);
	g_object_unref (msg);
}

static void
do_cancel_while_reading_test (void)
{
	SoupSession *session;

	g_test_bug ("637741");
	g_test_bug ("676038");

	session = soup_test_session_new (NULL);
	do_cancel_while_reading_test_for_session (session);
	soup_test_session_abort_unref (session);
}

static void
do_cancel_while_reading_req_test_for_session (SoupSession *session,
					      guint flags)
{
	SoupMessage *msg;
	GUri *uri;
	GCancellable *cancellable;
	GError *error = NULL;

	uri = g_uri_parse_relative (base_uri, "/slow", SOUP_HTTP_URI_FLAGS, NULL);
	msg = soup_message_new_from_uri ("GET", uri);
	g_uri_unref (uri);

	cancellable = g_cancellable_new ();
	soup_test_request_send (session, msg, cancellable, flags, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error (&error);

	g_object_unref (msg);
	g_object_unref (cancellable);
}

static void
do_cancel_while_reading_immediate_req_test (void)
{
	SoupSession *session;
	guint flags;

	g_test_bug ("692310");

	flags = SOUP_TEST_REQUEST_CANCEL_IMMEDIATE;

	session = soup_test_session_new (NULL);
	do_cancel_while_reading_req_test_for_session (session, flags);
	soup_test_session_abort_unref (session);
}

static void
do_cancel_while_reading_delayed_req_test (void)
{
	SoupSession *session;
	guint flags;

	flags = SOUP_TEST_REQUEST_CANCEL_SOON;

	session = soup_test_session_new (NULL);
	do_cancel_while_reading_req_test_for_session (session, flags);
	soup_test_session_abort_unref (session);
}

static void
do_cancel_while_reading_preemptive_req_test (void)
{
	SoupSession *session;
	guint flags;

	g_test_bug ("637039");

	flags = SOUP_TEST_REQUEST_CANCEL_PREEMPTIVE;

	session = soup_test_session_new (NULL);
	do_cancel_while_reading_req_test_for_session (session, flags);
	soup_test_session_abort_unref (session);
}

static void
do_msg_flags_test (void)
{
	SoupMessage *msg;

	msg = soup_message_new ("GET", "http://foo.org");

	/* Flags are initially empty */
	g_assert_cmpuint (soup_message_get_flags (msg), ==, 0);
	g_assert_false (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT));

	/* Set a single flag */
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	g_assert_cmpuint (soup_message_get_flags (msg), ==, SOUP_MESSAGE_NO_REDIRECT);
	g_assert_true (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT));
	g_assert_false (soup_message_query_flags (msg, SOUP_MESSAGE_NEW_CONNECTION));

	/* Add another flag */
	soup_message_add_flags (msg, SOUP_MESSAGE_NEW_CONNECTION);
	g_assert_cmpuint (soup_message_get_flags (msg), ==, (SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_NEW_CONNECTION));
	g_assert_true (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_NEW_CONNECTION));

	/* Add an existing flag */
	soup_message_add_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	g_assert_cmpuint (soup_message_get_flags (msg), ==, (SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_NEW_CONNECTION));
        g_assert_true (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_NEW_CONNECTION));

	/* Remove a single flag */
	soup_message_remove_flags (msg, SOUP_MESSAGE_NEW_CONNECTION);
	g_assert_cmpuint (soup_message_get_flags (msg), ==, SOUP_MESSAGE_NO_REDIRECT);
        g_assert_true (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT));
        g_assert_false (soup_message_query_flags (msg, SOUP_MESSAGE_NEW_CONNECTION));

	/* Remove a non-existing flag */
	soup_message_remove_flags (msg, SOUP_MESSAGE_NEW_CONNECTION);
	g_assert_cmpuint (soup_message_get_flags (msg), ==, SOUP_MESSAGE_NO_REDIRECT);
        g_assert_true (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT));
        g_assert_false (soup_message_query_flags (msg, SOUP_MESSAGE_NEW_CONNECTION));

	/* Add a set of flags */
	soup_message_add_flags (msg, SOUP_MESSAGE_NEW_CONNECTION | SOUP_MESSAGE_IDEMPOTENT | SOUP_MESSAGE_DO_NOT_USE_AUTH_CACHE);
	g_assert_cmpuint (soup_message_get_flags (msg), ==, (SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_NEW_CONNECTION | SOUP_MESSAGE_IDEMPOTENT | SOUP_MESSAGE_DO_NOT_USE_AUTH_CACHE));
	g_assert_true (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_NEW_CONNECTION | SOUP_MESSAGE_IDEMPOTENT | SOUP_MESSAGE_DO_NOT_USE_AUTH_CACHE));

	/* Remove a set of flags */
	soup_message_remove_flags (msg, (SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_IDEMPOTENT));
	g_assert_cmpuint (soup_message_get_flags (msg), ==, (SOUP_MESSAGE_NEW_CONNECTION | SOUP_MESSAGE_DO_NOT_USE_AUTH_CACHE));
	g_assert_true (soup_message_query_flags (msg, SOUP_MESSAGE_NEW_CONNECTION | SOUP_MESSAGE_DO_NOT_USE_AUTH_CACHE));

	/* Remove all flags */
	soup_message_set_flags (msg, 0);
	g_assert_cmpuint (soup_message_get_flags (msg), ==, 0);
        g_assert_false (soup_message_query_flags (msg, SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_NEW_CONNECTION | SOUP_MESSAGE_IDEMPOTENT | SOUP_MESSAGE_DO_NOT_USE_AUTH_CACHE));

	g_object_unref (msg);
}

static void
do_connection_id_test (void)
{
        SoupSession *session = soup_test_session_new (NULL);
        SoupMessage *msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);

        /* Test that the ID is set for each new connection and that it is
         * cached after the message is completed */

        g_assert_cmpuint (soup_message_get_connection_id (msg), ==, 0);

        guint status = soup_test_session_send_message (session, msg);

        g_assert_cmpuint (status, ==, SOUP_STATUS_OK);
        g_assert_cmpuint (soup_message_get_connection_id (msg), ==, 1);

        status = soup_test_session_send_message (session, msg);

        g_assert_cmpuint (status, ==, SOUP_STATUS_OK);
        g_assert_cmpuint (soup_message_get_connection_id (msg), ==, 2);

	GUri *uri = g_uri_parse_relative (base_uri, "/redirect", SOUP_HTTP_URI_FLAGS, NULL);
	soup_message_set_uri (msg, uri);
        g_uri_unref (uri);

        status = soup_test_session_send_message (session, msg);

        g_assert_cmpuint (status, ==, SOUP_STATUS_OK);
        g_assert_cmpuint (soup_message_get_connection_id (msg), ==, 3);

        g_object_unref (msg);
        soup_test_session_abort_unref (session);
}

static void
do_remote_address_test (void)
{
        SoupSession *session;
        SoupMessage *msg1, *msg2;
        GBytes *body;

        session = soup_test_session_new (NULL);

        msg1 = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
        g_assert_null (soup_message_get_remote_address (msg1));
        body = soup_test_session_async_send (session, msg1, NULL, NULL);
        g_assert_nonnull (soup_message_get_remote_address (msg1));
        g_bytes_unref (body);

        /* In case of reusing an idle conection, we still get a remote address */
        msg2 = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
        g_assert_null (soup_message_get_remote_address (msg2));
        body = soup_test_session_async_send (session, msg2, NULL, NULL);
        g_assert_cmpuint (soup_message_get_connection_id (msg1), ==, soup_message_get_connection_id (msg2));
        g_assert_true (soup_message_get_remote_address (msg1) == soup_message_get_remote_address (msg2));
        g_bytes_unref (body);
        g_object_unref (msg2);

        /* We get a new one if we force a new connection */
        msg2 = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
        soup_message_add_flags (msg2, SOUP_MESSAGE_NEW_CONNECTION);
        g_assert_null (soup_message_get_remote_address (msg2));
        body = soup_test_session_async_send (session, msg2, NULL, NULL);
        g_assert_nonnull (soup_message_get_remote_address (msg2));
        g_assert_cmpuint (soup_message_get_connection_id (msg1), !=, soup_message_get_connection_id (msg2));
        g_assert_false (soup_message_get_remote_address (msg1) == soup_message_get_remote_address (msg2));
        g_bytes_unref (body);
        g_object_unref (msg2);

        g_object_unref (msg1);
        soup_test_session_abort_unref (session);
}

int
main (int argc, char **argv)
{
	SoupAuthDomain *auth_domain;
	int ret;

	test_init (argc, argv, NULL);

	server = soup_test_server_new (SOUP_TEST_SERVER_IN_THREAD);
	soup_server_add_handler (server, NULL, server_callback, NULL, NULL);
	base_uri = soup_test_server_get_uri (server, "http", NULL);

	auth_domain = soup_auth_domain_basic_new (
		"realm", "misc-test",
		"auth-callback", auth_callback,
		NULL);
        soup_auth_domain_add_path (auth_domain, "/auth");
	soup_server_add_auth_domain (server, auth_domain);
	g_object_unref (auth_domain);

	g_test_add_func ("/misc/bigheader", do_host_big_header);
	g_test_add_func ("/misc/host", do_host_test);
	g_test_add_func ("/misc/callback-unref/msg", do_callback_unref_test);
	g_test_add_func ("/misc/msg-reuse", do_msg_reuse_test);
	g_test_add_func ("/misc/early-abort/msg", do_early_abort_test);
	g_test_add_func ("/misc/accept-language", do_accept_language_test);
	g_test_add_func ("/misc/cancel-while-reading/msg", do_cancel_while_reading_test);
	g_test_add_func ("/misc/cancel-while-reading/req/immediate", do_cancel_while_reading_immediate_req_test);
	g_test_add_func ("/misc/cancel-while-reading/req/delayed", do_cancel_while_reading_delayed_req_test);
	g_test_add_func ("/misc/cancel-while-reading/req/preemptive", do_cancel_while_reading_preemptive_req_test);
	g_test_add_func ("/misc/msg-flags", do_msg_flags_test);
        g_test_add_func ("/misc/connection-id", do_connection_id_test);
        g_test_add_func ("/misc/remote-address", do_remote_address_test);

	ret = g_test_run ();

	g_uri_unref (base_uri);
	soup_test_server_quit_unref (server);

	test_cleanup ();
	return ret;
}
