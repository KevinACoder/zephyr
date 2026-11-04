/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * <sys/time.h> for the imported net80211 sources on Zephyr.
 *
 * The BSD-world prelude (components/net80211/ports/compat/prelude.h) includes
 * <sys/time.h> unconditionally: one file-scope constant in ieee80211_input.c is
 * a `struct timeval`, and the component's other ports all run on libcs that ship
 * the header.  Zephyr's minimal libc does not - it has <sys/_timeval.h> and
 * <time.h>, and no <sys/time.h> at all.
 *
 * This shim is the missing header, and it is deliberately the whole of it: the
 * type, and nothing else.  No gettimeofday(), no settimeofday(), no timezone
 * machinery - the stack reads the clock through osKernelGetTickCount(), and a
 * POSIX time surface nothing calls would only invite someone to call it.
 *
 * It lives under the module (not in freewificam's ports/compat) so the FreeRTOS
 * and ThreadX images keep resolving <sys/time.h> to their own libc.
 */

#ifndef FWCAM_SHIM_SYS_TIME_H_
#define FWCAM_SHIM_SYS_TIME_H_

#include <sys/_timeval.h>

#endif /* FWCAM_SHIM_SYS_TIME_H_ */
