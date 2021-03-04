/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef __COMPILER_H
#define __COMPILER_H

#define NINE_ATTR_PRINTF(index, check) __attribute__((format(printf, index, check)))
#define NINE_ATTR_ALIGNED(alignment) __attribute__((aligned(alignment)))

#define ZeroMemory(p, s) memset(p, 0, s)
#define IsEqualGUID(a, b) !memcmp(a, b, sizeof(GUID))
#define InterlockedIncrement(p) __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p) __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST)

#endif /* __COMPILER_H */
