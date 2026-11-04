/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The image's entry point is not here.
 *
 * `main()` comes from the freewificam module: it is the stack's own board entry
 * (banner, CMSIS-RTOS2 facade, case-runner thread, scheduler hand-off), compiled
 * into the module's platform library.  A `main()` in this file would be a second
 * definition of the symbol, and the module's is the one whose behaviour the
 * ladder's anchors describe.
 *
 * The file exists because Zephyr's `app` target needs at least one translation
 * unit.  Its content is a build-identity string and nothing else.
 */

#include <zephyr/toolchain.h>

BUILD_ASSERT(CONFIG_FREEWIFICAM, "this image is built around the freewificam module");

__attribute__((used)) static const char fwcam_sample_identity[] =
	"freewificam rtl8xxxu 8188EU over CherryUSB";
