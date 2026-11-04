/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * <sys/errno.h> for the imported net80211 sources on Zephyr.
 *
 * components/net80211/ports/compat/sys/errno.h forwards with
 * #include_next <sys/errno.h>, because on the ports it was written for the C
 * library keeps errno in its own <sys/errno.h> (newlib does).  Zephyr's minimal
 * libc keeps everything in <errno.h> and ships no <sys/errno.h>; this file is
 * what the forwarding lookup lands on.
 *
 * It must stay last on the include path (see CMakeLists.txt), otherwise the
 * forwarding header would find itself.
 */

#ifndef FWCAM_SHIM_SYS_ERRNO_H_
#define FWCAM_SHIM_SYS_ERRNO_H_

#include <errno.h>

#endif /* FWCAM_SHIM_SYS_ERRNO_H_ */
