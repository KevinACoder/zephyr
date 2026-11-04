/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Force-included ahead of every freewificam translation unit.
 *
 * Zephyr puts its whole Kconfig namespace on the compiler command line
 * (-imacros autoconf.h, CMakeLists.txt), so every CONFIG_* symbol of the image
 * is visible to every source in it.  freewificam has its own, much smaller
 * namespace in the same spelling, and eight names exist in both trees with
 * different meanings:
 *
 *   SHELL   freewificam: start the CherrySH console.  Zephyr: the shell
 *           subsystem is built into the image.
 *   BOARD, DEBUG, ASSERT, SMP, PM, MODULES, ACPI: same spelling, unrelated
 *           meanings.
 *
 * Of these, only SHELL and SMP are actually read by code compiled here
 * (app/main.c reads SHELL, linuxkpi's netdevice.h reads SMP through
 * IS_ENABLED), but all eight are dropped so a later freewificam change cannot
 * silently pick up a Zephyr meaning.
 *
 * Why #undef and not -U: -U is applied before the -imacros file, so the
 * preprocessor sees the macro defined again afterwards.  A forced-include
 * header runs after -imacros, so the #undef sticks (verified in both command
 * line orders during the port).
 *
 * The last step re-establishes freewificam's own namespace: the image's
 * generated freewificam_config.h is included here, first, so every later
 * inclusion of it (build_defs.h does exactly that) is a no-op and no Zephyr
 * value can sit in front of a freewificam one.
 */

#ifndef FREEWIFICAM_ZEPHYR_SHIELD_H
#define FREEWIFICAM_ZEPHYR_SHIELD_H

/* Names both trees define, with unrelated meanings. */
#undef CONFIG_SHELL
#undef CONFIG_DEBUG
#undef CONFIG_ASSERT
#undef CONFIG_BOARD
#undef CONFIG_SMP
#undef CONFIG_PM
#undef CONFIG_MODULES
#undef CONFIG_ACPI

/*
 * CONFIG_SMP is deliberately left undefined rather than set to 0.
 *
 * freewificam reads it once, through IS_ENABLED(), which answers 0 for an
 * undefined name - so "single core" is what the code sees.  Zephyr's own
 * headers ask a different question: irq.h writes `#ifdef CONFIG_SMP`, and a
 * macro defined to 0 is still defined, which would turn every irq_lock() into
 * z_smp_global_lock() - a kernel symbol this image does not link.
 */

/* The image's own configuration, restored ahead of everything else. */
#include "freewificam_config.h"

#endif /* FREEWIFICAM_ZEPHYR_SHIELD_H */
