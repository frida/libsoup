/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * soup-tls-interaction.c: TLS interaction implementation
 *
 * Copyright (C) 2021 Igalia S.L.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-tls-interaction.h"

struct _SoupTlsInteraction {
        GTlsInteraction parent;
};

typedef struct {
        SoupConnection *conn;
} SoupTlsInteractionPrivate;

G_DEFINE_FINAL_TYPE_WITH_PRIVATE (SoupTlsInteraction, soup_tls_interaction, G_TYPE_TLS_INTERACTION)

static void
soup_tls_interaction_request_certificate_async (GTlsInteraction             *tls_interaction,
                                                GTlsConnection              *connection,
                                                GTlsCertificateRequestFlags  flags,
                                                GCancellable                *cancellable,
                                                GAsyncReadyCallback          callback,
                                                gpointer                     user_data)
{
        SoupTlsInteractionPrivate *priv = soup_tls_interaction_get_instance_private (SOUP_TLS_INTERACTION (tls_interaction));
        GTask *task;

        task = g_task_new (tls_interaction, cancellable, callback, user_data);
        if (priv->conn)
                soup_connection_request_tls_certificate (priv->conn, connection, task);
        else
                g_task_return_int (task, G_TLS_INTERACTION_FAILED);
        g_object_unref (task);
}

static GTlsInteractionResult
soup_tls_interaction_request_certificate_finish (GTlsInteraction *tls_interaction,
                                                 GAsyncResult    *result,
                                                 GError         **error)
{
        int task_result;

        task_result = g_task_propagate_int (G_TASK (result), error);
        return task_result != -1 ? task_result : G_TLS_INTERACTION_FAILED;
}

static void
soup_tls_interaction_ask_password_async (GTlsInteraction    *tls_interaction,
                                         GTlsPassword       *password,
                                         GCancellable       *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer            user_data)
{
        SoupTlsInteractionPrivate *priv = soup_tls_interaction_get_instance_private (SOUP_TLS_INTERACTION (tls_interaction));
        GTask *task;

        task = g_task_new (tls_interaction, cancellable, callback, user_data);
        if (priv->conn)
                soup_connection_request_tls_certificate_password (priv->conn, password, task);
        else
                g_task_return_int (task, G_TLS_INTERACTION_FAILED);
        g_object_unref (task);
}

static GTlsInteractionResult
soup_tls_interaction_ask_password_finish (GTlsInteraction *tls_interaction,
                                          GAsyncResult    *result,
                                          GError         **error)
{
        int task_result;

        task_result = g_task_propagate_int (G_TASK (result), error);
        return task_result != -1 ? task_result : G_TLS_INTERACTION_FAILED;
}

static void
soup_tls_interaction_finalize (GObject *object)
{
        SoupTlsInteractionPrivate *priv = soup_tls_interaction_get_instance_private (SOUP_TLS_INTERACTION (object));

        if (priv->conn) {
                g_object_remove_weak_pointer (G_OBJECT (priv->conn), (gpointer*)&priv->conn);
                priv->conn = NULL;
        }

        G_OBJECT_CLASS (soup_tls_interaction_parent_class)->finalize (object);
}

static void
soup_tls_interaction_init (SoupTlsInteraction *interaction)
{
}

static void
soup_tls_interaction_class_init (SoupTlsInteractionClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GTlsInteractionClass *interaction_class = G_TLS_INTERACTION_CLASS (klass);

        object_class->finalize = soup_tls_interaction_finalize;

        interaction_class->request_certificate_async = soup_tls_interaction_request_certificate_async;
        interaction_class->request_certificate_finish = soup_tls_interaction_request_certificate_finish;
        interaction_class->ask_password_async = soup_tls_interaction_ask_password_async;
        interaction_class->ask_password_finish = soup_tls_interaction_ask_password_finish;
}

GTlsInteraction *
soup_tls_interaction_new (SoupConnection *conn)
{
        GTlsInteraction *interaction;
        SoupTlsInteractionPrivate *priv;

        interaction = g_object_new (SOUP_TYPE_TLS_INTERACTION, NULL);
        priv = soup_tls_interaction_get_instance_private (SOUP_TLS_INTERACTION (interaction));
        priv->conn = conn;
        g_object_add_weak_pointer (G_OBJECT (priv->conn), (gpointer*)&priv->conn);

        return interaction;
}
