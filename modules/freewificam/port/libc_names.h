/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Two names freewificam defines that Zephyr's C library also defines.
 *
 * Renamed rather than removed, because both are contract in freewificam:
 *
 *  - vprintf: components/net80211/ports/osal/cmsis2/osal_cmsis2.c defines it as
 *    the imported NetBSD sources' continuation-print outlet (ieee80211_input.c
 *    and ieee80211_netbsd.c call it in their discard paths).  Its body routes
 *    through the stack's own logger, which owns the line stamp and the anchor
 *    promotion; Zephyr's vprintf would print the same line through the console
 *    hook instead, outside that discipline.
 *  - bsearch: components/linuxkpi/rtos/linuxkpi_compat.c carries Linux's entry
 *    point (lib/bsearch.c).  Its behaviour is the C library's, so this image
 *    could drop it - but that file is shared with the other backends, and a
 *    rename keeps the Linux face intact while the C library's own symbol stays
 *    the only `bsearch` in the link.
 *
 * Both are macros rather than #undef + redefinition because the definitions and
 * the call sites are in different translation units (and, for bsearch, different
 * libraries): every world that sees the name sees the same rename.
 */

#ifndef FREEWIFICAM_ZEPHYR_LIBC_NAMES_H
#define FREEWIFICAM_ZEPHYR_LIBC_NAMES_H

#define vprintf fwcam_net80211_vprintf
#define bsearch lkpi_bsearch

#endif /* FREEWIFICAM_ZEPHYR_LIBC_NAMES_H */
