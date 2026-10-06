/*
 * probe_srv.c — 通用探针的卡端（k1om）一侧。
 *
 * 与 probe_cli.c 配对，用一条简单的命令协议覆盖若干 API 的细粒度行为：
 *   CMD_ALIGN  : 宿主报告一次注册尝试的结果（卡端只记录，用于汇总）
 *   CMD_MSG    : 收 N 字节并原样回显（验证消息通道在各尺寸下的完整性）
 *   CMD_POLL   : 静默一段指定时长，用于宿主的 poll 语义测试
 *   CMD_FENCE  : 宿主发 fence_mark，卡端 fence_wait 后回报
 *   CMD_DONE   : 结束
 *
 * 构建：k1om_cc probe_srv.c -lscif -o probe_srv   （由 run_extra.sh 负责）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <scif.h>

#define PORT 20011
#define MAGIC 0x5150524f4245ULL   /* "QPROBE" */

struct msg_t {
	unsigned long long magic;
	unsigned long long cmd;
	unsigned long long a;   /* 尺寸 / 时长 / 期望值 */
	unsigned long long b;   /* 结果回填 */
};

#define CMD_ALIGN  1
#define CMD_MSG    2
#define CMD_POLL   3
#define CMD_FENCE  4
#define CMD_DONE   5

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* 简单 32 位校验和，两端同函数 */
static unsigned int cksum(const unsigned char *p, unsigned long long n)
{
	unsigned int h = 2166136261u;
	unsigned long long i;
	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

int main(int argc, char **argv)
{
	scif_epd_t l, c;
	struct scif_portID pid;
	struct msg_t m;
	unsigned char *buf;
	unsigned long long maxmsg = (argc > 1) ? strtoull(argv[1], NULL, 0) : (1ULL << 20);
	int i;

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (!(l = scif_open())) { printf("PROBE_SRV: scif_open failed\n"); return 1; }
	if (scif_bind(l, PORT) < 0) { printf("PROBE_SRV: bind %d failed: %s\n", PORT, strerror(errno)); return 1; }
	if (scif_listen(l, 4) < 0) { printf("PROBE_SRV: listen failed: %s\n", strerror(errno)); return 1; }
	printf("PROBE_SRV: listening on %d (maxmsg %llu)\n", PORT, maxmsg);

	if ((c = scif_accept(l, &pid, NULL, SCIF_ACCEPT_SYNC)) < 0) {
		printf("PROBE_SRV: accept failed: %s\n", strerror(errno));
		return 1;
	}
	printf("PROBE_SRV: accepted from node %d port %d\n", pid.node, pid.port);

	if (!(buf = malloc(maxmsg))) { printf("PROBE_SRV: malloc %llu failed\n", maxmsg); return 1; }

	for (;;) {
		if (scif_recv(c, &m, sizeof(m), SCIF_RECV_BLOCK) != (int)sizeof(m)) {
			printf("PROBE_SRV: recv cmd failed: %s\n", strerror(errno));
			break;
		}
		if (m.magic != MAGIC) { printf("PROBE_SRV: bad magic 0x%llx\n", m.magic); break; }

		if (m.cmd == CMD_DONE) {
			printf("PROBE_SRV: done\n");
			m.b = 0;
			scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK);
			break;
		}
		if (m.cmd == CMD_ALIGN) {
			/* 宿主报告一次注册尝试结果；卡端仅确认 */
			printf("PROBE_SRV: align report len=%llu ok=%llu\n", m.a, m.b);
			m.b = 0;
			scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK);
			continue;
		}
		if (m.cmd == CMD_MSG) {
			unsigned long long n = m.a, got = 0;
			if (n > maxmsg) { m.b = 1; scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK); continue; }
			while (got < n) {
				int r = scif_recv(c, buf + got, (size_t)(n - got), SCIF_RECV_BLOCK);
				if (r <= 0) break;
				got += (unsigned long long)r;
			}
			/* 回显：先回长度与校验和，再回数据 */
			m.b = cksum(buf, got);
			scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK);
			if (got) scif_send(c, buf, (size_t)got, SCIF_SEND_BLOCK);
			printf("PROBE_SRV: msg want=%llu got=%llu cksum=0x%x\n", n, got, (unsigned)cksum(buf, got));
			continue;
		}
		if (m.cmd == CMD_POLL) {
			double t0 = now_s();
			while (now_s() - t0 < (double)m.a / 1000.0) usleep(2000);
			m.b = (unsigned long long)((now_s() - t0) * 1000.0);
			scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK);
			printf("PROBE_SRV: poll slept %llu ms\n", m.b);
			continue;
		}
		if (m.cmd == CMD_FENCE) {
			/* 等待宿主 fence_mark 的信号后回报 */
			unsigned int cookie = 0xfeed0000u | (unsigned int)m.a;
			if (scif_fence_wait(c, &cookie, sizeof(cookie), SCIF_FENCE_INIT_SELF) < 0) {
				printf("PROBE_SRV: fence_wait failed: %s\n", strerror(errno));
				m.b = 1;
			} else {
				m.b = cookie;
			}
			scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK);
			printf("PROBE_SRV: fence wait -> 0x%llx\n", m.b);
			continue;
		}
		printf("PROBE_SRV: unknown cmd %llu\n", m.cmd);
		break;
	}

	scif_close(c);
	scif_close(l);
	free(buf);
	(void)i;
	return 0;
}
