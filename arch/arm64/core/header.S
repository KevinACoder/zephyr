/*
 * Copyright (c) 2020 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/linker/sections.h>
#include <zephyr/arch/cpu.h>

#if CONFIG_MMU_PAGE_SIZE == 4096 || defined(CONFIG_ARM_MPU)
#define HEADER_PGSIZE	1
#elif CONFIG_MMU_PAGE_SIZE == 16384
#define HEADER_PGSIZE	2
#elif CONFIG_MMU_PAGE_SIZE == 65536
#define HEADER_PGSIZE	3
#else
#define HEADER_PGSIZE 0
#warning "Can't determine page size for header flags"
#endif

/*
 * Flags field of the arm64 Image header (Documentation/arch/arm64/booting.rst):
 * bits 2:1 carry the page size, bit 3 tells the bootloader that the 2 MiB
 * aligned base of the image may be anywhere in RAM, i.e. it must not relocate
 * the image to the start of RAM. Zephyr runs at its link address, so the
 * image has to be entered where the bootloader loaded it.
 */
#define HEADER_FLAGS	((HEADER_PGSIZE << 1) | BIT(3))

_ASM_FILE_PROLOGUE

SECTION_SUBSEC_FUNC(image_header,_image_header_section,_image_header)
	b	__start				// branch to kernel start
	.long	0				// reserved
	.quad	0				// Image load offset from start
						// of RAM, little-endian

	.quad	_flash_used			// Effective size of kernel
						// image, little-endian

	.quad	HEADER_FLAGS			// Informative flags,
						// little-endian

	.quad	0				// reserved
	.quad	0				// reserved
	.quad	0				// reserved
	.ascii	"ARM\x64"			// Magic number
	.long	0				// reserved
