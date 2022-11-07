/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003, Ximian, Inc.
 */

#ifndef __SOUP_TYPES_H__
#define __SOUP_TYPES_H__ 1

#include <gio/gio.h>

#include "soup-version.h"
#include "soup-status.h"

G_BEGIN_DECLS

#define _SOUP_ATOMIC_INTERN_STRING(variable, value) ((const char *)(g_atomic_pointer_get (&(variable)) ? (variable) : (g_atomic_pointer_set (&(variable), (gpointer)g_intern_static_string (value)), (variable))))

G_END_DECLS

#endif /* __SOUP_TYPES_H__ */
