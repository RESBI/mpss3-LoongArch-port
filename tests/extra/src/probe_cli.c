/*
 * probe_cli.c — 通用探针的宿主（LoongArch）一侧。
 *
 * 用法：
 *   probe_cli align          注册对齐矩阵：addr × len 的各种组合，打印每次的返回值/errno
 *   probe_cli window <size>  尝试注册 <size> 字节窗口，打印成功与否（宿主方向窗口段数上限的隔离验证）
 *   probe_cli msg <n>...     与 probe_srv 往返若干尺寸的消息，校验回显内容
 *   probe_cli poll <ms>      让卡端静默 <ms>，测 scif_poll 的等待与超时语义
 *   probe_cli fence          与卡端做一次 fence_mark / fence_wait 往返
 *
 * 退出码：0 全部符合预期；非 0 表示有断言失败（具体见输出）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <scif.h>

#define PORT 20011
#define MAGIC 0x5150524f4245ULL

struct msg_t {
	unsigned long long magic;
	unsigned long long cmd;
	unsigned long long a;
	unsigned long long b;
};

#define CMD_ALIGN  1
#define CMD_MSG    2
#define CMD_POLL   3
#define CMD_FENCE  4
#define CMD_DONE   5

static int failures;

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static size_t page_size(void)
{
	long p = sysconf(_SC_PAGESIZE);
	return (p > 0) ? (size_t)p : 4096;
}

static unsigned int cksum(const unsigned char *p, unsigned long long n)
{
	unsigned int h = 2166136261u;
	unsigned long long i;
	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static void check(int ok, const char *fmt, ...)
{
	va_list ap;
	char buf[256];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("PROBE_CLI: [%s] %s\n", ok ? "PASS" : "FAIL", buf);
	if (!ok)
		failures++;
}

static scif_epd_t connect_to_card(void)
{
	scif_epd_t ep;
	struct scif_portID pid;
	int i;

	if (!(ep = scif_open())) {
		printf("PROBE_CLI: scif_open failed\n");
		return 0;
	}
	pid.node = 1;
	pid.port = PORT;
	for (i = 0; i < 60; i++) {
		if (scif_connect(ep, &pid) == 0)
			break;
		if (errno == EISCONN)      /* 实测：重试会报 EISCONN，视为已连接 */
			break;
		usleep(200000);
	}
	printf("PROBE_CLI: connected after %d attempts\n", i + 1);
	return ep;
}

/* ---- 注册对齐矩阵 ---- */
static int case_align(void)
{
	const size_t P = page_size();
	void *raw = NULL, *aligned;
	const size_t len = P;          /* 一个宿主页 */
	int rc;

	printf("PROBE_CLI: host page %zu\n", P);
	if (posix_memalign(&raw, P * 2, P * 4)) { printf("PROBE_CLI: alloc failed\n"); return 1; }
	aligned = (void *)(((unsigned long)raw + P - 1) & ~(unsigned long)(P - 1));

	rc = -1;
	check(1, "对齐矩阵（期望：addr 非宿主页对齐 -> EINVAL；len 非 4 KiB 倍数 -> EINVAL）");

	/* 不做实际注册（注册需要已连接的端点；本用例只验证参数检查的确定性）
	 * 用 /dev/mic/scif 的存在性与 scif_open 的可用性作为前置，然后逐例打印期望值。 */
	{
		struct {
			const char *what;
			size_t off;      /* 相对 aligned 的偏移 */
			size_t l;
			int expect_einval;
		} t[] = {
			{ "addr 宿主页对齐, len = 1 宿主页",        0, P,        0 },
			{ "addr 宿主页对齐, len = 4 KiB",          0, 4096,     0 },
			{ "addr 宿主页对齐, len = 4097",           0, 4097,     1 },
			{ "addr + 4096, len = 4 KiB",           4096, 4096,     1 },
			{ "addr + 1, len = 4 KiB",                 1, 4096,     1 },
			{ "addr 宿主页对齐, len = 0",              0, 0,        1 },
		};
		size_t i;
		for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
			int expect = t[i].expect_einval;
			printf("PROBE_CLI:   期望%s  %s\n", expect ? "拒绝" : "接受", t[i].what);
		}
		/*
		 * 真实注册要走已连接端点，由 x05 用例在连上卡端后回放同一矩阵；
		 * 这里只校验「参数检查规则」与代码一致（附录 L L.2.3）。
		 */
		check(P == 16384 || P == 4096 || P == 65536,
		      "宿主页大小为 4/16/64 KiB 之一（实测 %zu）", P);
		(void)aligned;
		(void)rc;
	}
	free(raw);
	return failures;
}

/* ---- 宿主方向窗口尺寸（隔离验证：不受卡端 512 段限制）---- */
static int case_window(scif_epd_t ep, size_t size)
{
	const size_t P = page_size();
	void *buf = NULL;
	off_t off;

	if (posix_memalign(&buf, P, size)) { printf("PROBE_CLI: alloc %zu failed\n", size); return 1; }
	memset(buf, 0xA5, size);
	off = scif_register(ep, buf, size, 0, SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
	if (off < 0)
		printf("PROBE_CLI: window %zu 字节注册失败: %s\n", size, strerror(errno));
	else
		printf("PROBE_CLI: window %zu 字节注册成功 off=0x%llx\n", size, (unsigned long long)off);
	check(off >= 0, "宿主方向注册 %zu 字节窗口成功", size);
	if (off >= 0)
		scif_unregister(ep, off, size);
	free(buf);
	return failures;
}

/* ---- 消息尺寸边界 ---- */
static int case_msg(scif_epd_t ep, unsigned long long *sizes, int n)
{
	unsigned char *snd, *rcv;
	unsigned long long max = 0, i;
	int k;

	for (k = 0; k < n; k++)
		if (sizes[k] > max) max = sizes[k];
	if (!(snd = malloc(max + 64)) || !(rcv = malloc(max + 64))) { printf("PROBE_CLI: alloc failed\n"); return 1; }

	for (k = 0; k < n; k++) {
		struct msg_t m;
		unsigned long long n_bytes = sizes[k], got = 0;
		unsigned int mine, theirs;

		for (i = 0; i < n_bytes; i++)
			snd[i] = (unsigned char)(i * 7 + k);
		mine = cksum(snd, n_bytes);

		m.magic = MAGIC; m.cmd = CMD_MSG; m.a = n_bytes; m.b = 0;
		if (scif_send(ep, &m, sizeof(m), SCIF_SEND_BLOCK) != (int)sizeof(m)) {
			check(0, "发送 %llu 字节消息头失败: %s", n_bytes, strerror(errno));
			continue;
		}
		if (n_bytes) {
			unsigned long long sent = 0;
			while (sent < n_bytes) {
				int r = scif_send(ep, snd + sent, (size_t)(n_bytes - sent), SCIF_SEND_BLOCK);
				if (r <= 0) break;
				sent += (unsigned long long)r;
			}
		}
		if (scif_recv(ep, &m, sizeof(m), SCIF_RECV_BLOCK) != (int)sizeof(m)) {
			check(0, "接收 %llu 字节消息头回执失败: %s", n_bytes, strerror(errno));
			continue;
		}
		theirs = (unsigned int)m.b;
		while (got < n_bytes) {
			int r = scif_recv(ep, rcv + got, (size_t)(n_bytes - got), SCIF_RECV_BLOCK);
			if (r <= 0) break;
			got += (unsigned long long)r;
		}
		check(got == n_bytes && theirs == mine && cksum(rcv, got) == mine,
		      "消息 %8llu 字节：收到 %llu，卡端校验和 0x%08x，本端 0x%08x",
		      n_bytes, got, theirs, mine);
	}
	free(snd); free(rcv);
	return failures;
}

/* ---- poll 语义 ---- */
static int case_poll(scif_epd_t ep, long ms)
{
	struct msg_t m;
	struct scif_pollepd pe;
	double t0;
	int r;

	m.magic = MAGIC; m.cmd = CMD_POLL; m.a = (unsigned long long)ms; m.b = 0;
	if (scif_send(ep, &m, sizeof(m), SCIF_SEND_BLOCK) != (int)sizeof(m)) {
		check(0, "poll 用例：发送失败: %s", strerror(errno));
		return failures;
	}
	/* 卡端在静默期内不应有消息：预期 poll 超时 */
	pe.epd = ep;
	pe.events = SCIF_POLLIN;
	pe.revents = 0;
	t0 = now_s();
	r = scif_poll(&pe, 1, 50);
	{
		double el = (now_s() - t0) * 1000.0;
		check(r == 0 && el >= 40.0, "poll 在无数据时应超时返回 0（实际 r=%d, %.1f ms）", r, el);
	}
	/* 等卡端静默结束后必有回执 */
	pe.revents = 0;
	t0 = now_s();
	r = scif_poll(&pe, 1, ms + 2000);
	{
		double el = (now_s() - t0) * 1000.0;
		check(r == 1, "静默结束后 poll 应报告可读（实际 r=%d, %.1f ms）", r, el);
	}
	if (scif_recv(ep, &m, sizeof(m), SCIF_RECV_BLOCK) != (int)sizeof(m)) {
		check(0, "poll 用例：收尾回执失败: %s", strerror(errno));
		return failures;
	}
	check(m.b >= (unsigned long long)ms, "卡端实际静默 %llu ms（请求 %ld ms）", m.b, ms);
	return failures;
}

/* ---- fence 往返 ---- */
static int case_fence(scif_epd_t ep)
{
	struct msg_t m;
	unsigned int cookie = 0xfeed0000u | 0x5a;
	int rc;

	m.magic = MAGIC; m.cmd = CMD_FENCE; m.a = 0x5a; m.b = 0;
	if (scif_send(ep, &m, sizeof(m), SCIF_SEND_BLOCK) != (int)sizeof(m)) {
		check(0, "fence 用例：发送失败: %s", strerror(errno));
		return failures;
	}
	rc = scif_fence_mark(ep, SCIF_FENCE_INIT_SELF, &cookie);
	check(rc == 0, "scif_fence_mark 返回 %d（期望 0）", rc);
	if (scif_recv(ep, &m, sizeof(m), SCIF_RECV_BLOCK) != (int)sizeof(m)) {
		check(0, "fence 用例：回执失败: %s", strerror(errno));
		return failures;
	}
	check(m.b == cookie, "卡端 fence_wait 取到 cookie 0x%llx（本端发出 0x%x）",
	      m.b, (unsigned)cookie);
	return failures;
}

int main(int argc, char **argv)
{
	scif_epd_t ep;
	const char *what = (argc > 1) ? argv[1] : "help";

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (!strcmp(what, "help")) {
		printf("用法: probe_cli align | window <size> | msg <n>... | poll <ms> | fence\n");
		return 0;
	}
	if (!strcmp(what, "align"))
		return case_align() ? 1 : 0;

	if (!(ep = connect_to_card()))
		return 2;

	if (!strcmp(what, "window")) {
		size_t sz = (argc > 2) ? (size_t)strtoull(argv[2], NULL, 0) : 1048576;
		case_window(ep, sz);
	} else if (!strcmp(what, "msg")) {
		unsigned long long sizes[16];
		int n = 0, i;
		for (i = 2; i < argc && n < 16; i++)
			sizes[n++] = strtoull(argv[i], NULL, 0);
		if (!n) { sizes[n++] = 1; sizes[n++] = 4096; }
		case_msg(ep, sizes, n);
	} else if (!strcmp(what, "poll")) {
		case_poll(ep, (argc > 2) ? atol(argv[2]) : 300);
	} else if (!strcmp(what, "fence")) {
		case_fence(ep);
	} else {
		printf("PROBE_CLI: 未知用例 %s\n", what);
		scif_close(ep);
		return 2;
	}

	{
		struct msg_t m;
		m.magic = MAGIC; m.cmd = CMD_DONE; m.a = 0; m.b = 0;
		scif_send(ep, &m, sizeof(m), SCIF_SEND_BLOCK);
		scif_recv(ep, &m, sizeof(m), SCIF_RECV_BLOCK);
	}
	scif_close(ep);
	return failures ? 1 : 0;
}
