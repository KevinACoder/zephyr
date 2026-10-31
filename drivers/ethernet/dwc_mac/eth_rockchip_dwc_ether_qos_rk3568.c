/*
 * Driver for Synopsys DesignWare MAC
 *
 * SPDX-FileCopyrightText: Copyright (c) 2026 zhugengyu
 * SPDX-License-Identifier: Apache-2.0
 *
 * Rockchip RK3568 specific glue.
 *
 * Unlike RK3588 there are no clock, pinctrl, reset or GPIO drivers for
 * RK3568 in the tree, so the CRU clock gates, the GRF pin mux and the RGMII
 * delay configuration are programmed through the syscon regions directly.
 * The values below are the ones verified on the RK3568 E4AP5G1-ITX board
 * (gmac1, RTL8211F, RGMII, 1 Gbps).
 *
 * RK3568 has two GMAC instances. The register addresses and the GRF/CRU
 * offsets of both are listed so that either port can be enabled from
 * devicetree without touching this file.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dwmac_plat, CONFIG_ETHERNET_LOG_LEVEL);

#define DT_DRV_COMPAT rockchip_rk3568_gmac

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/phy.h>
#include <zephyr/sys/util.h>

#include "eth_dwmac_priv.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_NOCACHE_MEMORY),
	     "RK3568 DWMAC requires CONFIG_NOCACHE_MEMORY for DMA descriptors");

/* The DMA bus master interface is a 64-bit AXI4 interface on this IP */
#define DATA_BUS_WIDTH 64

DWMAC_ASSERT_BUFFER_ALIGNMENT(DATA_BUS_WIDTH);

/* MMC counters present in the controller */
#define RK3568_GMAC_MMC_COUNTERS(X)                                                                \
	X(TX_OCTET_COUNT_GOOD_BAD)                                                                 \
	X(TX_PACKET_COUNT_GOOD_BAD)                                                                \
	X(TX_UNDERFLOW_ERROR_PACKETS)                                                              \
	X(TX_CARRIER_ERROR_PACKETS)                                                                \
	X(TX_OCTET_COUNT_GOOD)                                                                     \
	X(TX_PACKET_COUNT_GOOD)                                                                    \
	X(TX_PAUSE_PACKETS)                                                                        \
	X(RX_PACKETS_COUNT_GOOD_BAD)                                                               \
	X(RX_OCTET_COUNT_GOOD_BAD)                                                                 \
	X(RX_OCTET_COUNT_GOOD)                                                                     \
	X(RX_MULTICAST_PACKETS_GOOD)                                                               \
	X(RX_CRC_ERROR_PACKETS)                                                                    \
	X(RX_OVERSIZE_PACKETS_GOOD)                                                                \
	X(RX_LENGTH_ERROR_PACKETS)                                                                 \
	X(RX_PAUSE_PACKETS)                                                                        \
	X(RX_FIFO_OVERFLOW_PACKETS)                                                                \
	X(RX_WATCHDOG_ERROR_PACKETS)                                                               \
	X(RXIPV4_GOOD_PACKETS)                                                                     \
	X(RXIPV4_HEADER_ERROR_PACKETS)                                                             \
	X(RXIPV6_GOOD_PACKETS)                                                                     \
	X(RXIPV6_HEADER_ERROR_PACKETS)

/* One pin of a GMAC pin group: GRF pin number and its mux function */
struct rk3568_gmac_pin {
	uint8_t pin;
	uint8_t func;
};

/*
 * Per-instance bus configuration. Offsets are relative to the CRU, GRF or
 * GPIO bank base address of the SoC.
 */
struct rk3568_gmac_port {
	uint32_t base;
	/* CRU_CLKGATE_CON: gate bits, active low, cleared to enable the clocks */
	uint16_t clkgate_con;
	uint16_t clkgate_mask;
	/* CRU_SOFTRST_CON: reset bits, active low, cleared to release the MAC */
	uint16_t softrst_con;
	uint16_t softrst_mask;
	/* CRU_CLKSEL_CON: MAC clock mux, 0 selects the 125 MHz RGMII path */
	uint16_t clksel_con;
	/* GRF_GMACn_CON0: RGMII TX/RX clock delay values */
	uint16_t maccon0;
	uint16_t maccon0_mask;
	uint16_t maccon0_val;
	/* GRF_GMACn_CON1: interface select and clock delay enables */
	uint16_t maccon1;
	uint16_t maccon1_mask;
	uint16_t maccon1_val;
	/* GRF pin mux for the RGMII/MDIO pin group */
	uint16_t iomux;
	const struct rk3568_gmac_pin *pins;
	uint8_t num_pins;
	/* GRF IO route register, 0 when the port does not have one */
	uint16_t route;
	uint32_t route_val;
	/* PHY reset line, as bit index inside the GPIO bank */
	uint8_t reset_pin;
};

/* gmac0: rgmii bus + clock, tx_bus2/rx_bus2, miim */
static const struct rk3568_gmac_pin rk3568_gmac0_pins[] = {
	{3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {11, 1},
	{12, 1}, {13, 1}, {14, 1}, {15, 2}, {16, 2}, {19, 2}, {20, 2},
};

/* gmac1m0: tx_bus2/rx_bus2, rgmii clock, clkinout and miim */
static const struct rk3568_gmac_pin rk3568_gmac1_pins[] = {
	{13, 3}, {14, 3}, {2, 3}, {3, 3}, {15, 3}, {9, 3}, {10, 3}, {4, 3},
	{5, 3}, {11, 3}, {6, 3}, {7, 3}, {16, 3}, {20, 3}, {21, 3},
};

#define RK3568_GMAC0_REG_ADDR 0xfe2a0000U
#define RK3568_GMAC1_REG_ADDR 0xfe010000U

static const struct rk3568_gmac_port rk3568_gmac0_port = {
	.base = RK3568_GMAC0_REG_ADDR,
	.clkgate_con = 0x33c,
	.clkgate_mask = 0x1f70,
	.softrst_con = 0x434,
	.softrst_mask = 0x0080,
	.clksel_con = 0x17c,
	.maccon0 = 0x380,
	.maccon0_mask = 0x7f7f,
	.maccon0_val = 0x264f,
	.maccon1 = 0x384,
	.maccon1_mask = 0x0073,
	.maccon1_val = 0x0013,
	.iomux = 0x020,
	.pins = rk3568_gmac0_pins,
	.num_pins = ARRAY_SIZE(rk3568_gmac0_pins),
	.reset_pin = 9,
};

static const struct rk3568_gmac_port rk3568_gmac1_port = {
	.base = RK3568_GMAC1_REG_ADDR,
	.clkgate_con = 0x344,
	.clkgate_mask = 0x07dc,
	.softrst_con = 0x438,
	.softrst_mask = 0x1000,
	.clksel_con = 0x184,
	.maccon0 = 0x388,
	.maccon0_mask = 0x7f7f,
	.maccon0_val = 0x033c,
	.maccon1 = 0x38c,
	.maccon1_mask = 0x0073,
	.maccon1_val = 0x0013,
	.iomux = 0x040,
	.pins = rk3568_gmac1_pins,
	.num_pins = ARRAY_SIZE(rk3568_gmac1_pins),
	.route = 0x300,
	.route_val = 0x0100,
	.reset_pin = 18,
};

#define RK3568_GMAC_ID(n)                                                                          \
	((DT_INST_REG_ADDR(n) == RK3568_GMAC0_REG_ADDR) ? 0 : 1)

struct rk3568_gmac_config {
	struct dwmac_config dwmac;
	const struct device *grf;
	const struct device *cru;
	/* Syscon mapping of the GPIO bank holding the PHY reset line */
	const struct device *reset_gpio;
	const struct rk3568_gmac_port *port;
	uint8_t id;
	int (*platform_init)(const struct device *dev);
};

/* Register writes in the Rockchip "write enable in the upper half word" style */
static void rk3568_gmac_field_write(const struct device *syscon, uint16_t offset, uint16_t mask,
				    uint32_t value)
{
	syscon_write_reg(syscon, offset, ((uint32_t)mask << 16) | (value & mask));
}

static void rk3568_gmac_clock_enable(const struct rk3568_gmac_config *cfg)
{
	const struct rk3568_gmac_port *port = cfg->port;

	/* The gate and soft reset bits are active low: writing 0 releases them */
	syscon_write_reg(cfg->cru, port->clkgate_con, (uint32_t)port->clkgate_mask << 16);
	syscon_write_reg(cfg->cru, port->softrst_con, (uint32_t)port->softrst_mask << 16);
	/* All-zero selector: RGMII mode, 125 MHz source */
	syscon_write_reg(cfg->cru, port->clksel_con, 0xffffU << 16);
}

/* GRF iomux register of one pin: four bits per pin, four pins per 32-bit word */
static void rk3568_gmac_iomux(const struct rk3568_gmac_config *cfg)
{
	const struct rk3568_gmac_port *port = cfg->port;

	if (port->route != 0U) {
		/* Full register word: write-enable mask in bits[31:16] */
		syscon_write_reg(cfg->grf, port->route, port->route_val);
	}

	for (uint8_t i = 0; i < port->num_pins; i++) {
		const struct rk3568_gmac_pin *pin = &port->pins[i];
		uint16_t offset = port->iomux + (pin->pin / 8) * 8 + ((pin->pin % 8) / 4) * 4;
		uint32_t shift = (pin->pin % 4) * 4;

		syscon_write_reg(cfg->grf, offset, (0xfU << (shift + 16)) | (pin->func << shift));
	}
}

static void rk3568_gmac_rgmii_setup(const struct rk3568_gmac_config *cfg)
{
	const struct rk3568_gmac_port *port = cfg->port;

	/*
	 * CON1 (interface select and delay enables) first, then CON0 (the
	 * delay values): this is the order verified on the board.
	 */
	rk3568_gmac_field_write(cfg->grf, port->maccon1, port->maccon1_mask, port->maccon1_val);
	rk3568_gmac_field_write(cfg->grf, port->maccon0, port->maccon0_mask, port->maccon0_val);
}

/*
 * GPIO bank, version 2 layout: data at 0x00/0x04, direction at 0x08/0x0c,
 * value in bits[15:0] and write-enable in bits[31:16]. The pin is driven
 * through the syscon mapping of the bank, there is no GPIO driver to call.
 */
static void rk3568_gmac_phy_reset(const struct rk3568_gmac_config *cfg)
{
	const struct device *gpio = cfg->reset_gpio;
	const struct rk3568_gmac_port *port = cfg->port;
	uint16_t offset = (port->reset_pin < 16) ? 0 : 4;
	uint32_t bit = BIT(port->reset_pin & 15);
	uint32_t we = bit << 16;

	/* Data first, then the direction: active low, assert 20 ms, release */
	syscon_write_reg(gpio, offset, we);
	syscon_write_reg(gpio, offset + 8, we | bit);
	k_msleep(20);
	syscon_write_reg(gpio, offset, we | bit);
	/* The RTL8211F needs 100 ms to boot and re-latch its straps */
	k_msleep(100);
}

/* Bus init and device instances */

int dwmac_bus_init(const struct device *dev)
{
	const struct rk3568_gmac_config *cfg = dev->config;

	if (!device_is_ready(cfg->grf) || !device_is_ready(cfg->cru)) {
		LOG_ERR("GRF or CRU syscon not ready");
		return -ENODEV;
	}

	rk3568_gmac_clock_enable(cfg);
	rk3568_gmac_iomux(cfg);
	rk3568_gmac_rgmii_setup(cfg);
	rk3568_gmac_phy_reset(cfg);

	/*
	 * Give the PHY and the transmit clock divider time to settle before
	 * dwmac_probe issues the MAC software reset.
	 */
	k_msleep(1);

	LOG_DBG("GMAC%u bus init done", cfg->id);

	return 0;
}

/*
 * The RGMII transmit clock is not divided by the GRF on RK3568; the CRU
 * mac_speed mux would have to change for 100M/10M operation. The bench runs
 * at 1 Gbps, so the fixed 125 MHz configuration is left untouched.
 */
void dwmac_platform_link_speed_changed(const struct device *dev, enum phy_link_speed speed)
{
	ARG_UNUSED(dev);

	if (!PHY_LINK_IS_SPEED_1000M(speed)) {
		LOG_WRN("link speed %d is not supported, running at 125 MHz", speed);
	}
}

/*
 * While DMA_MODE.SWR is asserted the GMAC clock mux has to take the RMII
 * (62.5 MHz) path or the reset never clears; leaving it in RMII afterwards
 * would keep the MAC at the wrong rate, hence the exit call.
 */
void dwmac_platform_swr_quirk(const struct device *dev, bool enter)
{
	const struct rk3568_gmac_config *cfg = dev->config;

	syscon_write_reg(cfg->cru, cfg->port->clksel_con, (0x3U << 16) | (enter ? 1U : 0U));
}

#define RK3568_GMAC_RESET_GPIO(n) \
	DEVICE_DT_GET(DT_INST_PHANDLE(n, rockchip_phy_reset_bank))

#define RK3568_GMAC_DEFINE(n)                                                                      \
	BUILD_ASSERT(DT_INST_REG_ADDR(n) == RK3568_GMAC0_REG_ADDR ||                               \
			     DT_INST_REG_ADDR(n) == RK3568_GMAC1_REG_ADDR,                         \
		     "unknown RK3568 GMAC register address");                                      \
	BUILD_ASSERT(DT_INST_ENUM_HAS_VALUE(n, phy_connection_type, rgmii),                        \
		     "RK3568 GMAC requires phy-connection-type = \"rgmii\"");                      \
                                                                                                   \
	static struct dwmac_dma_desc __nocache_noinit                                              \
		__aligned(CONFIG_DCACHE_LINE_SIZE) dwmac_tx_descs_##n[NB_TX_DESCS];                \
	static struct dwmac_dma_desc __nocache_noinit                                              \
		__aligned(CONFIG_DCACHE_LINE_SIZE) dwmac_rx_descs_##n[NB_RX_DESCS];                \
                                                                                                   \
	DWMAC_MMC_COUNTERS_DEFINE(rk3568_gmac_mmc_##n, RK3568_GMAC_MMC_COUNTERS);                  \
                                                                                                   \
	static int rk3568_gmac_platform_init_##n(const struct device *dev)                         \
	{                                                                                          \
		const struct net_eth_mac_config mac_cfg = NET_ETH_MAC_DT_INST_CONFIG_INIT(n);      \
		struct dwmac_priv *p = dev->data;                                                  \
		uintptr_t tx_phys = k_mem_phys_addr(dwmac_tx_descs_##n);                           \
		uintptr_t rx_phys = k_mem_phys_addr(dwmac_rx_descs_##n);                           \
		int ret;                                                                           \
                                                                                                   \
		p->tx_descs = dwmac_tx_descs_##n;                                                  \
		p->rx_descs = dwmac_rx_descs_##n;                                                  \
		p->tx_descs_phys = (struct dwmac_dma_desc *)tx_phys;                               \
		p->rx_descs_phys = (struct dwmac_dma_desc *)rx_phys;                               \
                                                                                                   \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), dwmac_isr,                  \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
                                                                                                   \
		ret = net_eth_mac_load(&mac_cfg, p->mac_addr);                                     \
		if (ret == -ENODATA) {                                                             \
			LOG_DBG("No MAC address configured");                                      \
			return 0;                                                                  \
		}                                                                                  \
		if (ret < 0) {                                                                     \
			LOG_ERR("Failed to load MAC address (%d)", ret);                           \
			return ret;                                                                \
		}                                                                                  \
		return 0;                                                                          \
	}                                                                                          \
                                                                                                   \
	static const struct rk3568_gmac_config rk3568_gmac_config_##n = {                          \
		.dwmac = {                                                                         \
			DEVICE_MMIO_ROM_INIT(DT_DRV_INST(n)),                                      \
			.phy_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, phy_handle)),                  \
			DWMAC_MMC_CONFIG_INIT(rk3568_gmac_mmc_##n)                                 \
		},                                                                                 \
		.grf = DEVICE_DT_GET(DT_INST_PHANDLE(n, rockchip_grf)),                            \
		.cru = DEVICE_DT_GET(DT_INST_PHANDLE(n, rockchip_cru)),                            \
		.reset_gpio = RK3568_GMAC_RESET_GPIO(n),                                           \
		.id = RK3568_GMAC_ID(n),                                                           \
		.port = (DT_INST_REG_ADDR(n) == RK3568_GMAC0_REG_ADDR) ? &rk3568_gmac0_port :      \
									     &rk3568_gmac1_port,   \
		.platform_init = rk3568_gmac_platform_init_##n,                                    \
	};                                                                                         \
                                                                                                   \
	static struct dwmac_priv dwmac_instance_##n;

DT_INST_FOREACH_STATUS_OKAY(RK3568_GMAC_DEFINE)

int dwmac_platform_init(const struct device *dev)
{
	const struct rk3568_gmac_config *cfg = dev->config;

	DWMAC_REG_WRITE(MAC_CONF, MAC_CONF_DM);
	DWMAC_REG_WRITE(DMA_SYSBUS_MODE, DMA_SYSBUS_MODE_AAL | DMA_SYSBUS_MODE_EAME |
						 DMA_SYSBUS_MODE_FB | DMA_SYSBUS_MODE_BLEN16 |
						 DMA_SYSBUS_MODE_BLEN8 | DMA_SYSBUS_MODE_BLEN4 |
						 FIELD_PREP(DMA_SYSBUS_MODE_RD_OSR_LMT, 7) |
						 FIELD_PREP(DMA_SYSBUS_MODE_WR_OSR_LMT, 3));

	return cfg->platform_init(dev);
}

#define RK3568_GMAC_DEVICE_DEFINE(n)                                                               \
	ETH_NET_DEVICE_DT_INST_DEFINE(n, dwmac_probe, NULL, &dwmac_instance_##n,                   \
				      &rk3568_gmac_config_##n, CONFIG_ETH_INIT_PRIORITY,           \
				      &dwmac_api, NET_ETH_MTU);

DT_INST_FOREACH_STATUS_OKAY(RK3568_GMAC_DEVICE_DEFINE)
