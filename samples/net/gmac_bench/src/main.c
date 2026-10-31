/*
 * RK3568 DWC GMAC bench application.
 *
 * The image is brought up with a static IPv4 address so that the iperf2
 * client on the bench Pi can drive it without DHCP. The zperf library
 * provides the iperf2-compatible server; this file adds the pieces the
 * measurement needs on the board side:
 *
 *  - a `gmac` shell command dumping the driver's Ethernet statistics and
 *    the MAC's MMC counters (the receive FIFO overflow counter is the
 *    attribution point for loss reported by the client),
 *  - a periodic link/counter line so a serial capture is self-contained.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(gmac_bench, LOG_LEVEL_INF);

static struct net_if *bench_iface(void)
{
	return net_if_get_default();
}

static const struct device *bench_dev(void)
{
	struct net_if *iface = bench_iface();

	return iface != NULL ? net_if_get_device(iface) : NULL;
}

static void print_link(const struct shell *sh)
{
	struct net_if *iface = bench_iface();
	const struct net_linkaddr *addr;

	if (iface == NULL) {
		shell_error(sh, "no default interface");
		return;
	}

	addr = net_if_get_link_addr(iface);

	shell_fprintf(sh, SHELL_NORMAL, "iface up=%d carrier=%d mtu=%d\n",
		      net_if_is_up(iface) ? 1 : 0,
		      net_if_is_carrier_ok(iface) ? 1 : 0,
		      net_if_get_mtu(iface));
	shell_fprintf(sh, SHELL_NORMAL, "mac %02x:%02x:%02x:%02x:%02x:%02x\n",
		      addr->addr[0], addr->addr[1], addr->addr[2],
		      addr->addr[3], addr->addr[4], addr->addr[5]);
}

static void print_stats(const struct shell *sh)
{
	struct net_if *iface = bench_iface();
	struct net_stats_eth stats;

	if (iface == NULL) {
		shell_error(sh, "no default interface");
		return;
	}

	if (net_mgmt(NET_REQUEST_STATS_GET_ETHERNET, iface, &stats, sizeof(stats)) != 0) {
		shell_error(sh, "no Ethernet statistics");
		return;
	}

	shell_fprintf(sh, SHELL_NORMAL,
		      "eth rx_pkts=%u tx_pkts=%u rx_bytes=%u tx_bytes=%u\n",
		      (uint32_t)stats.pkts.rx, (uint32_t)stats.pkts.tx,
		      (uint32_t)stats.bytes.received, (uint32_t)stats.bytes.sent);
	shell_fprintf(sh, SHELL_NORMAL,
		      "eth rx_err=%u tx_err=%u crc=%u overrun=%u no_buf=%u\n",
		      (uint32_t)stats.errors.rx, (uint32_t)stats.errors.tx,
		      (uint32_t)stats.error_details.rx_crc_errors,
		      (uint32_t)stats.error_details.rx_over_errors,
		      (uint32_t)stats.error_details.rx_no_buffer_count);

	if (stats.vendor == NULL) {
		return;
	}

	for (size_t i = 0; stats.vendor[i].key != NULL; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "mmc %s=%u\n", stats.vendor[i].key,
			      (uint32_t)stats.vendor[i].value);
	}
}

static int cmd_gmac_stats(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	print_stats(sh);

	return 0;
}

static int cmd_gmac_link(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	print_link(sh);

	return 0;
}

static void periodic_report(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(report_work, periodic_report);

static void periodic_report(struct k_work *work)
{
	struct net_if *iface = bench_iface();

	ARG_UNUSED(work);

	if (iface != NULL) {
		LOG_INF("uptime=%lld ms carrier=%d", k_uptime_get(),
			net_if_is_carrier_ok(iface) ? 1 : 0);
	}

	k_work_reschedule(&report_work, K_SECONDS(10));
}

static int bench_init(void)
{
	const struct device *dev = bench_dev();

	if (dev == NULL) {
		LOG_ERR("no default Ethernet device");
		return -ENODEV;
	}

	LOG_INF("bench device %s ready=%d", dev->name, device_is_ready(dev) ? 1 : 0);

	k_work_reschedule(&report_work, K_SECONDS(10));

	return 0;
}

SYS_INIT(bench_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

SHELL_STATIC_SUBCMD_SET_CREATE(gmac_cmds,
	SHELL_CMD_ARG(stats, NULL, "Dump Ethernet and MMC counters", cmd_gmac_stats, 1, 0),
	SHELL_CMD_ARG(link, NULL, "Dump interface state and MAC address", cmd_gmac_link, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(gmac, &gmac_cmds, "RK3568 GMAC bench commands", NULL);
