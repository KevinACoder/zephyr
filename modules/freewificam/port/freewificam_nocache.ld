/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Where the USB DMA scratch files live.
 *
 * linuxkpi's CherryUSB adapter stages every transfer it cannot pass through (and
 * on this image that is every transfer, see below) into a static array declared
 * `__attribute__((section(".noncacheable")))`.  On the board images that section
 * is carved out of RAM and mapped non-cacheable by the port's own MMU code, which
 * is why the adapter is allowed to skip cache maintenance on it.
 *
 * On this image the array still has to be writable RAM - it is a DMA buffer the
 * CPU fills and the controller reads - so it is folded into .bss and left
 * cacheable.  That is safe with the window symbols undefined: the adapter's
 * chu_in_window() then reports "not in the window" for every buffer and stages
 * plus maintains each transfer, which is the correct treatment for cacheable
 * memory.  Making the window real would need an MMU region with MT_NORMAL_NC,
 * and that is a later step, not a default.
 *
 * Written as raw input sections, not as a SECTION_PROLOGUE: Zephyr's BSS
 * snippet is #include'd *inside* the bss output section's braces, so a nested
 * section definition there is a linker syntax error.  The cost is that the
 * bounds get bss alignment rather than 64-byte alignment; nothing reads them yet
 * (see above), and the array itself carries its own aligned(64).
 */

. = ALIGN(64);
__noncacheable_region_start = .;
KEEP(*(.noncacheable))
. = ALIGN(64);
__noncacheable_region_end = .;
