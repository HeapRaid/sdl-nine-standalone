/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef __COMPILER_H
#define __COMPILER_H

#define NINE_ATTR_PRINTF(index, check) __attribute__((format(printf, index, check)))
#define NINE_ATTR_ALIGNED(alignment) __attribute__((aligned(alignment)))

#define ZeroMemory(p, s) memset(p, 0, s)
#define IsEqualGUID(a, b) !memcmp(a, b, sizeof(GUID))
#define InterlockedIncrement(p) __sync_fetch_and_add(p, 1) + 1; __sync_synchronize()
#define InterlockedDecrement(p) __sync_fetch_and_sub(p, 1) - 1; __sync_synchronize()

#endif /* __COMPILER_H */
