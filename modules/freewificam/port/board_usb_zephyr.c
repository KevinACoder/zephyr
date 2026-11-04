/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MMIO apertures of the RK3568 USB domain, mapped for Zephyr's MMU.
 *
 * freewificam's board layer (tests/board/rtos/platform/board/rk3568-itx/usb/)
 * is bare-metal register code: it writes hardcoded physical addresses for the
 * four controllers, the CRU/PMUCRU/PMU power island, the GPIO3 VBUS pins and
 * the two USB2PHY syscons.  Zephyr's arm64 boot path maps only the image, the
 * GIC and the devicetree memory regions, so the first store to any of those
 * addresses would be a translation fault - a silent hang at the console, since
 * the fault happens before the USB stack can print anything.
 *
 * The fix is one device_map() per aperture, executed before the first USB
 * access.  With CONFIG_KERNEL_DIRECT_MAP the mapping is placed at the physical
 * address itself, so the bare-metal code keeps working unmodified - no wrapper,
 * no offset arithmetic, no change to freewificam.
 *
 * Sizes are the smallest power-of-two window that covers each block's used
 * registers, rounded up to pages; they are not the SoC's full block sizes
 * because mapping more than the driver touches would hide an address mistake.
 * The two PHY syscons are mapped at their GRF base as the board header defines
 * them (0xFDCA0000 / 0xFDCA8000), each 64 KiB.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/printk.h>

/* One window: physical base, size and the name the report prints. */
struct fwcam_mmio_window {
	const char *name;
	uintptr_t phys;
	size_t size;
	mm_reg_t virt;
};

static struct fwcam_mmio_window windows[] = {
	{ "ehci0",        0xFD800000UL, 0x10000UL, 0 },	/* USB2 host0 EHCI */
	{ "ehci1",        0xFD880000UL, 0x10000UL, 0 },	/* USB2 host1 EHCI */
	{ "xhci0",        0xFD000000UL, 0x10000UL, 0 },	/* USB3 host0 xHCI */
	{ "xhci1",        0xFCC00000UL, 0x10000UL, 0 },	/* USB3 host1 xHCI */
	{ "cru",          0xFDD20000UL, 0x10000UL, 0 },	/* controller resets */
	{ "pmucru",       0xFDD00000UL, 0x10000UL, 0 },	/* PHY clock gates */
	{ "pmu",          0xFDD90000UL, 0x10000UL, 0 },	/* power island */
	{ "gpio3",        0xFE760000UL, 0x10000UL, 0 },	/* VBUS enable pins */
	{ "usb2phy0_grf", 0xFDCA0000UL, 0x10000UL, 0 },	/* PHY0 suspend cfg */
	{ "usb2phy1_grf", 0xFDCA8000UL, 0x10000UL, 0 },	/* PHY1 suspend cfg */
};

/*
 * Map every aperture.  Idempotent: a second call sees the windows already
 * mapped and returns the same addresses (device_map() on an already mapped
 * range would fail, so the state is remembered here).
 *
 * Returns 0 when all ten are mapped and every one landed at its physical
 * address, which is what the bare-metal board layer assumes.  A non-zero return
 * means the image must not touch the USB domain at all: the failure is reported
 * with the window's name so the boot log says which one.
 */
int fwcam_usb_mmio_map(void)
{
	static int mapped;
	unsigned int i;

	if (mapped != 0) {
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(windows); i++) {
		struct fwcam_mmio_window *w = &windows[i];

		device_map(&w->virt, w->phys, w->size, K_MEM_CACHE_NONE);
		if (w->virt == 0U) {
			printk("fwcam: mmio map failed for %s at 0x%lx\n",
			       w->name, (unsigned long)w->phys);
			return -1;
		}
		if (w->virt != w->phys) {
			/* The board layer holds hardcoded physical addresses;
			 * a different virtual address means it must not run. */
			printk("fwcam: mmio %s mapped at 0x%lx, expected 0x%lx\n",
			       w->name, (unsigned long)w->virt,
			       (unsigned long)w->phys);
			return -1;
		}
	}

	mapped = 1;
	printk("fwcam: mmio windows mapped (%u, direct map)\n",
	       (unsigned int)ARRAY_SIZE(windows));
	return 0;
}

/*
 * Run before main(): the apertures must be reachable by the time the first case
 * touches a controller, and mapping them at boot also means a mapping failure is
 * one line in the banner rather than a fault inside the USB bring-up.
 */
static int fwcam_usb_mmio_sys_init(void)
{
	(void)fwcam_usb_mmio_map();
	return 0;
}

SYS_INIT(fwcam_usb_mmio_sys_init, APPLICATION, 0);
