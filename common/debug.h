/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef __COMMON_DEBUG_H
#define __COMMON_DEBUG_H

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#include <d3d9types.h>

#include "compiler.h"

enum __nine_debug_class
{
    __NINE_DBCL_FIXME,
    __NINE_DBCL_ERR,
    __NINE_DBCL_WARN,
    __NINE_DBCL_TRACE,
};

extern unsigned char __nine_debug_flags;

const char *__nine_dbg_strdup(const char *s, size_t len);

static inline int __nine_dbg_log(enum __nine_debug_class dbcl, const char *function,
                                 const char *format, ...) NINE_ATTR_PRINTF(3, 4);
static inline int __nine_dbg_log(enum __nine_debug_class dbcl, const char *function,
                                 const char *format, ...)
{
    char buf[1024] NINE_ATTR_ALIGNED(16);
    va_list args;
    int n;

    static const char *const classes[] = { "fixme", "err", "warn", "trace" };
    n = sprintf(buf, "%s:d3d9nine:%s ", classes[dbcl], function);

    va_start(args, format);
    n += vsnprintf(buf + n, sizeof(buf) - n, format, args);
    va_end(args);

    return write(STDERR_FILENO, buf, n);
}

#define __NINE_DPRINTF(dbcl, args...) \
    do { \
        if (__nine_debug_flags & (1 << dbcl)) \
            __nine_dbg_log(dbcl, __FUNCTION__, args); \
    } while (0)

#define FIXME(args...) __NINE_DPRINTF(__NINE_DBCL_FIXME, args)
#define ERR(args...) __NINE_DPRINTF(__NINE_DBCL_ERR, args)
#define WARN(args...) __NINE_DPRINTF(__NINE_DBCL_WARN, args)
#define TRACE(args...) __NINE_DPRINTF(__NINE_DBCL_TRACE, args)

static inline const char *nine_dbg_sprintf(const char *format, ...) NINE_ATTR_PRINTF(1, 2);
static inline const char *nine_dbg_sprintf(const char *format, ...)
{
    char buffer[256] NINE_ATTR_ALIGNED(16);
    va_list args;
    size_t len;

    va_start(args, format);
    len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return __nine_dbg_strdup(buffer, len);
}

/* see WINE's wine_dbgstr_guid() */
static inline const char *nine_dbgstr_guid( const GUID *id )
{
    if (!id) return "(null)";
    return nine_dbg_sprintf( "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                             id->Data1, id->Data2, id->Data3,
                             id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
                             id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7] );
}

/* see WINE's wine_dbgstr_point() */
static inline const char *nine_dbgstr_point( const POINT *pt )
{
    if (!pt) return "(null)";
    return nine_dbg_sprintf( "(%d,%d)", pt->x, pt->y );
}

/* see WINE's wine_dbgstr_rect() */
static inline const char *nine_dbgstr_rect( const RECT *rect )
{
    if (!rect) return "(null)";
    return nine_dbg_sprintf( "(%d,%d)-(%d,%d)", rect->left, rect->top,
                             rect->right, rect->bottom );
}

#endif /* __COMMON_DEBUG_H */
