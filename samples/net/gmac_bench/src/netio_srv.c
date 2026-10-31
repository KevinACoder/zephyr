/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * netio server: the board side of the netio wire protocol used by this
 * project's throughput instrument.
 *
 * The protocol is the one Arm's MDK-Middleware netio test speaks and the one
 * the RTOS line in this repository measures with, so the unmodified upstream
 * netio client on the lab host drives both ends:
 *
 *   - TCP control connection on port 18768,
 *   - UDP data on port 18768 (same number),
 *   - 8 byte control messages, two big endian words {cmd, data}:
 *       CMD_C2S (1): the client sends data, data = packet size. The server
 *                    acknowledges and counts the datagrams it receives.
 *       CMD_RES (3): the client asks for the result, data = packet count.
 *       CMD_S2C (2): the server sends data for the window duration, then
 *                    reports the number of datagrams it sent.
 *       CMD_QUIT (0): close the control connection.
 *
 * The C2S direction (host -> board) is what the receive-side measurements
 * use; S2C is implemented so that the same client binary can run its full
 * sequence without a special case on the host.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(netio_srv, LOG_LEVEL_INF);

#define NETIO_PORT	18768
#define NETIO_WINDOW_MS	6000
#define NETIO_BUF_SIZE	2048

#define CMD_QUIT	0
#define CMD_C2S		1
#define CMD_S2C		2
#define CMD_RES		3

struct netio_msg {
	uint32_t cmd;
	uint32_t data;
};

struct netio_state {
	int listen_fd;
	int conn_fd;
	int udp_fd;
	struct sockaddr_in peer;
	uint32_t pkt_size;
	uint32_t c2s_count;
	uint32_t s2c_count;
	bool running;
};

static struct netio_state srv = {
	.listen_fd = -1,
	.conn_fd = -1,
	.udp_fd = -1,
};

static uint8_t netio_buf[NETIO_BUF_SIZE] __aligned(64);

static int netio_udp_open(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(NETIO_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int fd;

	fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		LOG_ERR("udp socket: %d", -errno);
		return -errno;
	}

	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("udp bind: %d", -errno);
		zsock_close(fd);
		return -errno;
	}

	srv.udp_fd = fd;

	return 0;
}

static void netio_udp_drain(void)
{
	while (true) {
		ssize_t len;

		len = zsock_recvfrom(srv.udp_fd, netio_buf, sizeof(netio_buf),
				     ZSOCK_MSG_DONTWAIT, NULL, NULL);
		if (len < 0) {
			break;
		}

		srv.c2s_count++;
	}
}

static int netio_control_send(struct netio_msg *msg)
{
	ssize_t sent;

	sent = zsock_send(srv.conn_fd, msg, sizeof(*msg), 0);
	if (sent != (ssize_t)sizeof(*msg)) {
		LOG_ERR("control send: %d", sent < 0 ? -errno : -EIO);
		return -EIO;
	}

	return 0;
}

static void netio_s2c_run(uint32_t pkt_size, struct sockaddr_in *peer)
{
	uint32_t sent = 0;
	uint64_t end;

	if (pkt_size > sizeof(netio_buf)) {
		return;
	}

	peer->sin_port = htons(NETIO_PORT);
	memset(netio_buf, 0, pkt_size);

	end = k_uptime_get() + NETIO_WINDOW_MS;

	while (k_uptime_get() < end) {
		ssize_t len;

		len = zsock_sendto(srv.udp_fd, netio_buf, pkt_size, 0,
				   (struct sockaddr *)peer, sizeof(*peer));
		if (len < 0) {
			continue;
		}

		sent++;
	}

	srv.s2c_count = sent;
	LOG_INF("netio: s2c sent=%u pkt=%u", sent, pkt_size);
}

static void netio_c2s_begin(uint32_t pkt_size)
{
	srv.pkt_size = pkt_size;
	srv.c2s_count = 0;
}

static void netio_c2s_report(void)
{
	LOG_INF("netio: c2s rx=%u pkt=%u", srv.c2s_count, srv.pkt_size);
}

static void netio_handle_control(struct netio_msg *msg)
{
	struct netio_msg reply;

	switch (msg->cmd) {
	case CMD_C2S:
		netio_c2s_begin(msg->data);
		reply.cmd = sys_cpu_to_be32(CMD_C2S);
		reply.data = sys_cpu_to_be32(0);
		(void)netio_control_send(&reply);
		break;
	case CMD_RES:
		netio_c2s_report();
		reply.cmd = sys_cpu_to_be32(CMD_RES);
		reply.data = sys_cpu_to_be32(srv.c2s_count);
		(void)netio_control_send(&reply);
		break;
	case CMD_S2C:
		if (srv.udp_fd < 0) {
			(void)netio_udp_open();
		}
		netio_s2c_run(msg->data, &srv.peer);
		reply.cmd = sys_cpu_to_be32(CMD_RES);
		reply.data = sys_cpu_to_be32(srv.s2c_count);
		(void)netio_control_send(&reply);
		break;
	case CMD_QUIT:
	default:
		LOG_INF("netio: quit");
		if (srv.conn_fd >= 0) {
			zsock_close(srv.conn_fd);
			srv.conn_fd = -1;
		}
		break;
	}
}

static void netio_handle_conn(void)
{
	struct netio_msg msg;
	ssize_t len;

	len = zsock_recv(srv.conn_fd, &msg, sizeof(msg), ZSOCK_MSG_DONTWAIT);
	if (len < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			LOG_ERR("control recv: %d", -errno);
			zsock_close(srv.conn_fd);
			srv.conn_fd = -1;
		}
		return;
	}

	if (len == 0) {
		LOG_INF("netio: control closed by peer");
		zsock_close(srv.conn_fd);
		srv.conn_fd = -1;
		return;
	}

	if (len != (ssize_t)sizeof(msg)) {
		/* The 8 byte control messages never straddle segments in
		 * practice; a short read is treated as a protocol error.
		 */
		LOG_ERR("control short read %d", (int)len);
		zsock_close(srv.conn_fd);
		srv.conn_fd = -1;
		return;
	}

	msg.cmd = sys_be32_to_cpu(msg.cmd);
	msg.data = sys_be32_to_cpu(msg.data);

	netio_handle_control(&msg);
}

static void netio_accept(void)
{
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	int fd;

	fd = zsock_accept(srv.listen_fd, (struct sockaddr *)&addr, &addr_len);
	if (fd < 0) {
		LOG_ERR("accept: %d", -errno);
		return;
	}

	if (srv.conn_fd >= 0) {
		zsock_close(srv.conn_fd);
	}

	srv.conn_fd = fd;
	srv.peer = addr;
	LOG_INF("netio: control from %s:%d",
		zsock_inet_ntop(AF_INET, &addr.sin_addr, netio_buf,
				INET_ADDRSTRLEN),
		ntohs(addr.sin_port));
}

static void netio_thread(void *p1, void *p2, void *p3)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(NETIO_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	srv.listen_fd = zsock_socket(AF_INET, SOCK_STREAM, 0);
	if (srv.listen_fd < 0) {
		LOG_ERR("control socket: %d", -errno);
		return;
	}

	if (zsock_bind(srv.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    zsock_listen(srv.listen_fd, 1) < 0) {
		LOG_ERR("control bind/listen: %d", -errno);
		return;
	}

	if (netio_udp_open() < 0) {
		return;
	}

	LOG_INF("netio: server on port %d", NETIO_PORT);

	while (true) {
		struct zsock_pollfd fds[3];
		int nfds = 0;
		int ret;

		fds[nfds].fd = srv.listen_fd;
		fds[nfds].events = ZSOCK_POLLIN;
		nfds++;

		if (srv.conn_fd >= 0) {
			fds[nfds].fd = srv.conn_fd;
			fds[nfds].events = ZSOCK_POLLIN;
			nfds++;
		}

		if (srv.udp_fd >= 0) {
			fds[nfds].fd = srv.udp_fd;
			fds[nfds].events = ZSOCK_POLLIN;
			nfds++;
		}

		ret = zsock_poll(fds, nfds, -1);
		if (ret < 0) {
			LOG_ERR("poll: %d", -errno);
			continue;
		}

		for (int i = 0; i < nfds; i++) {
			if ((fds[i].revents & ZSOCK_POLLIN) == 0) {
				continue;
			}

			if (fds[i].fd == srv.listen_fd) {
				netio_accept();
			} else if (fds[i].fd == srv.conn_fd) {
				netio_handle_conn();
			} else if (fds[i].fd == srv.udp_fd) {
				netio_udp_drain();
			}
		}
	}
}

/* Cooperative priority: the drain loop must not be preempted in the middle of
 * a poll wakeup, but it always returns to the poll when the socket is empty so
 * the network RX thread gets the CPU back.
 */
K_THREAD_DEFINE(netio_tid, 4096, netio_thread, NULL, NULL, NULL,
		K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1), 0, 0);

static int cmd_netio_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "netio: c2s=%u (pkt=%u) s2c=%u", srv.c2s_count,
		    srv.pkt_size, srv.s2c_count);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(netio_cmds,
	SHELL_CMD(status, NULL, "netio counters", cmd_netio_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(netio, &netio_cmds, "netio server", NULL);
