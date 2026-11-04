/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linker sections freewificam's registries live in.
 *
 * The board images own their linker script and declare each of these by hand
 * (tests/board/rtos/platform/arch/aarch64/baremetal.ld).  On Zephyr the script
 * belongs to the architecture, so the module contributes the same sections as
 * a fragment: each registry is a section whose bounds are two symbols, walked
 * at init time.  The KEEP() discipline is what keeps the entries alive - they
 * are never referenced by address, only by the walk.
 *
 * This file is included through zephyr_linker_sources(ROM_SECTIONS ...), i.e.
 * it lands with the read-only data, NOT in the SECTIONS slot at the very end of
 * the arm64 script.  The placement matters for the image the board is given: the
 * arm64 script reserves RAM (bss, noinit - 4 MB of it here) *before* the
 * SECTIONS slot, and these registries are PROGBITS.  Put after noinit, objcopy
 * pads the binary from the load base to here, and a 900 KB image becomes 6 MB.
 * In the ROM region they share a load segment with the code.
 *
 * The DMA scratch area (.noncacheable) is NOT here - a writable buffer has no
 * business in a read-only load segment.  It lives in port/freewificam_nocache.ld,
 * linked with .bss.
 */

SECTION_DATA_PROLOGUE(freewificam_registries,,)
{
	/* CherryUSB class drivers (hub, and any class a later profile adds). */
	. = ALIGN(8);
	__usbh_class_info_start__ = .;
	KEEP(*(.usbh_class_info))
	__usbh_class_info_end__ = .;

	/* Linux module registry: module_usb_driver() publishes the driver's
	 * struct usb_driver * here and lkpi_usb_init() walks the bounds. */
	. = ALIGN(8);
	__lkpi_module_start = .;
	KEEP(*(.lkpi_module))
	__lkpi_module_end = .;

	/* module_init() entry points, run once by lkpi_module_init_run(). */
	. = ALIGN(8);
	__lkpi_init_start = .;
	KEEP(*(.lkpi_init))
	__lkpi_init_end = .;

	/* SDIO driver registry: unused on this image, kept because the seam
	 * helpers are compiled in and their section must not become an orphan. */
	. = ALIGN(8);
	__lkpi_sdio_driver_start = .;
	KEEP(*(.lkpi_sdio_driver))
	__lkpi_sdio_driver_end = .;

	/* The case table: BOARD_TEST() puts one {name, fn} per case here and the
	 * runner walks the bounds. */
	. = ALIGN(8);
	__board_tests_start = .;
	KEEP(*(BOARD_TESTS))
	__board_tests_end = .;
} GROUP_ROM_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)
