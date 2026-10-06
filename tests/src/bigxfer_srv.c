/* Card side (T8): receive a large transfer (default 4 GiB) into a big malloc'd
 * buffer, then checksum what actually arrived.
 *
 * Why this exists: the port's known weak spot is "big allocations and big
 * transfers". This program is the receiving end of that experiment: it mallocs
 * the whole region up front (touching every page so an out-of-memory shows up
 * here and not later), then registers a *small* window of it (1 MiB by default)
 * and lets the host fill that window piece by piece; each filled window is
 * copied into the big buffer at the right offset and acknowledged. If the window
 * happens to cover the whole region (small totals) the copy is skipped and the
 * host writes straight into the big buffer (mode 0, "direct"). Either way the
 * host is told exactly what happened, so the measurement is never silently
 * partial.
 *
 * Protocol (all messages are fixed-size structs over scif_send/recv):
 *   card -> host  hello {magic, mode, total, span, reg_off, window}
 *   host -> card  msg   {magic, kind=CHUNK, a=offset, b=len}   (windowed mode)
 *   card -> host  msg   {magic, kind=ACK}
 *   host -> card  msg   {magic, kind=DONE}
 *   card -> host  fin   {magic, checksum, seconds_us, span}
 *
 * Checksum: order- and position-sensitive 64-bit FNV-style hash over 8-byte
 * words (see cksum below). Both sides run the identical function; both ends are
 * little-endian, so raw words compare directly.
 *
 * build: k1om-cc -O0 -o bigxfer_srv bigxfer_srv.c -lscif
 * run:   ./bigxfer_srv [total_bytes] [max_window_bytes]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>
#include <scif.h>

#define PORT        20005
#define MAGIC_HELLO 0x8148ULL
#define MAGIC_MSG   0x8A17ULL
#define MAGIC_FIN   0x8F17ULL
#define KIND_CHUNK  1ULL
#define KIND_ACK    2ULL
#define KIND_DONE   3ULL

#define BIG_DEFAULT (4ULL * 1024 * 1024 * 1024)   /* 4 GiB */
#define WIN_DEFAULT (1ULL * 1024 * 1024)          /* 1 MiB */
#define WIN_MIN     (1ULL * 1024 * 1024)          /* never go below 1 MiB */

struct hello_t {
	unsigned long long magic;
	unsigned long long mode;      /* 0 = direct, 1 = windowed */
	unsigned long long total;     /* bytes the host is going to send */
	unsigned long long span;      /* bytes actually registered on the card */
	unsigned long long reg_off;   /* offset of the registered window */
	unsigned long long window;    /* chunk size used on the wire */
};

struct msg_t {
	unsigned long long magic;
	unsigned long long kind;
	unsigned long long a;
	unsigned long long b;
	unsigned long long c;      /* 宿主对该窗口数据算的 checksum（逐窗口校验用） */
};

struct fin_t {
	unsigned long long magic;
	unsigned long long checksum;
	unsigned long long seconds_us;
	unsigned long long span;
	unsigned long long win_bad;      /* 逐窗口校验失败的窗口数 */
	unsigned long long win_first_bad;/* 第一个失败的窗口号（0 表示没有） */
};

/* Identical on both sides: order- and position-sensitive. */
static unsigned long long cksum(const unsigned char *p, size_t n)
{
	unsigned long long h = 0x123456789ABCDEF0ULL ^ (unsigned long long)n;
	size_t i = 0;

	for (; i + 8 <= n; i += 8) {
		unsigned long long w;
		memcpy(&w, p + i, 8);
		w ^= (unsigned long long)(i >> 3);
		h ^= w;
		h *= 0x100000001B3ULL;
		h ^= h >> 29;
	}
	for (; i < n; i++) {
		h ^= p[i];
		h *= 0x100000001B3ULL;
	}
	return h;
}

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	unsigned long long total = BIG_DEFAULT, want_win = WIN_DEFAULT;
	unsigned long long span, win, off, done = 0, windows = 0;
	scif_epd_t ep, c;
	struct scif_portID peer;
	unsigned char *buf, *stage = NULL;
	off_t reg_off = -1;
	unsigned long long win_bad = 0, win_first_bad = 0;
	struct hello_t hello;
	struct msg_t m;
	struct fin_t fin;
	double t0;
	int rc, mode;
	size_t i;

	if (argc > 1) total = strtoull(argv[1], NULL, 0);
	if (argc > 2) want_win = strtoull(argv[2], NULL, 0);
	if (total == 0 || (total & 7) || (total & 4095)) {
		printf("BIGXFER_SRV: bad total %llu (need a multiple of 4096)\n", total);
		return 2;
	}
	if (want_win == 0) want_win = WIN_DEFAULT;

	/* 1. the big allocation: this is itself part of what T8 tests */
	t0 = now_s();
	buf = memalign(4096, (size_t)total);
	if (!buf) {
		printf("BIGXFER_SRV: malloc %llu FAILED (%s)\n",
		       total, strerror(errno));
		return 3;
	}
	for (i = 0; i < (size_t)total; i += 4096)   /* touch every page */
		buf[i] = 0;
	printf("BIGXFER_SRV: malloc %llu ok (%p, touched in %.2f s)\n",
	       total, (void *)buf, now_s() - t0);
	fflush(stdout);

	/* 2. how much of it can we expose to the host in one go? */
	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	if (scif_bind(ep, PORT) < 0) { perror("scif_bind"); return 1; }
	if (scif_listen(ep, 4) < 0) { perror("scif_listen"); return 1; }
	printf("BIGXFER_SRV: listening on port %d\n", PORT);
	fflush(stdout);

	c = (scif_epd_t)-1;
	rc = scif_accept(ep, &peer, &c, SCIF_ACCEPT_SYNC);
	if (rc != 0) { perror("scif_accept"); return 1; }
	printf("BIGXFER_SRV: accepted from node %u port %u\n",
	       (unsigned)peer.node, (unsigned)peer.port);
	fflush(stdout);

	/* Only ever register as much as the host asked for. Probing upward towards
	 * `total` was measured to be dangerous: a multi-megabyte registration plus a
	 * multi-megabyte RMA wedges both sides in the driver's window-reclaim path
	 * (D state, needs a reboot), so the window stays small and the transfer is
	 * chunked in many small RMAs instead. */
	span = (want_win < total) ? want_win : total;
	/*
	 * 暂存区必须与大缓冲【分开】：窗口是要被反复复用的，若它恰好落在大缓冲开头，
	 * 那么"窗口 0 的目的地"就是暂存区本身 —— 后续每个窗口的入站数据都会把窗口 0
	 * 刚放好的数据盖掉（实测：buf[1MiB..) 正确，头 1 MiB 里是最后一个窗口的数据，
	 * 两端 checksum 必然不等）。所以这里单独开一块 span 大小的暂存区来注册窗口。
	 */
	if (span != total) {
		stage = memalign(4096, (size_t)span);
		if (!stage) {
			printf("BIGXFER_SRV: staging malloc %llu FAILED (%s)\n",
			       span, strerror(errno));
			return 3;
		}
		memset(stage, 0, (size_t)span);
	}
	while (span >= WIN_MIN) {
		t0 = now_s();
		reg_off = scif_register(c, stage ? stage : buf, (size_t)span, 0,
					SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
		if (reg_off >= 0) {
			printf("BIGXFER_SRV: registered %llu bytes at 0x%llx (%.2f s)\n",
			       span, (unsigned long long)reg_off, now_s() - t0);
			break;
		}
		printf("BIGXFER_SRV: register %llu failed (%s), halving\n",
		       span, strerror(errno));
		fflush(stdout);
		span /= 2;
	}
	if (reg_off < 0) {
		printf("BIGXFER_SRV: could not register anything >= %llu\n", WIN_MIN);
		return 4;
	}

	if (span == total) {
		mode = 0;               /* direct: host writes into the big buffer */
		win = (want_win < total) ? want_win : total;   /* wire chunk size */
	} else {
		mode = 1;               /* windowed: reuse one window, copy chunks */
		win = span;
	}
	printf("BIGXFER_SRV: mode=%s span=%llu window=%llu\n",
	       mode == 0 ? "direct" : "windowed", span, win);
	fflush(stdout);

	memset(&hello, 0, sizeof(hello));
	hello.magic = MAGIC_HELLO;
	hello.mode = (unsigned long long)mode;
	hello.total = total;
	hello.span = span;
	hello.reg_off = (unsigned long long)reg_off;
	hello.window = win;
	if (scif_send(c, &hello, sizeof(hello), SCIF_SEND_BLOCK) != (int)sizeof(hello)) {
		perror("scif_send hello");
		return 1;
	}

	/* 3. let the host push the data; in windowed mode copy each chunk out of
	 *    the shared window before the next one lands on top of it */
	if (mode == 1) {
		while (1) {
			if (scif_recv(c, &m, sizeof(m), SCIF_RECV_BLOCK) != (int)sizeof(m)) {
				printf("BIGXFER_SRV: recv failed, aborting\n");
				return 1;
			}
			if (m.kind == KIND_DONE) break;
			if (m.kind != KIND_CHUNK) continue;
			if (m.a + m.b > total) {
				printf("BIGXFER_SRV: chunk out of range off=%llu len=%llu\n",
				       m.a, m.b);
				return 5;
			}
			/* 逐窗口校验：与宿主对该窗口数据算的值比对，差错可定位到窗口号 */
			{
				unsigned long long ck = cksum(stage, (size_t)m.b);
				if (ck != m.c) {
					win_bad++;
					if (!win_first_bad)
						win_first_bad = windows + 1;
					if (win_bad <= 3)
						printf("BIGXFER_SRV: WINDOW %llu BAD off=%llu len=%llu host=0x%llx card=0x%llx\n",
						       windows + 1, m.a, m.b, m.c, ck);
				}
				memcpy(buf + m.a, stage, (size_t)m.b);
			}
			done += m.b;
			windows++;
			if (windows <= 4 || windows % 256 == 0)
				printf("BIGXFER_SRV: window %llu off=%llu len=%llu total=%llu\n",
				       windows, m.a, m.b, done);
			m.magic = MAGIC_MSG;
			m.kind = KIND_ACK;
			m.a = m.b = 0;
			if (scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK) != (int)sizeof(m)) {
				printf("BIGXFER_SRV: ack failed\n");
				return 1;
			}
		}
	} else {
		off = 0;
		while (1) {
			if (scif_recv(c, &m, sizeof(m), SCIF_RECV_BLOCK) != (int)sizeof(m)) {
				printf("BIGXFER_SRV: recv failed, aborting\n");
				return 1;
			}
			if (m.kind == KIND_DONE) { done = m.a; break; }
		}
	}

	/* 4. checksum what actually arrived.
	 * 注意：窗口模式下同一个窗口被反复复用，累计 done 会超过 span（窗口大小），
	 * 所以夹紧的上限是【请求总量 total】，不是 span —— 早先写成 span 时，
	 * 卡端只校验了第一个窗口（实测：64 MiB 只校验 1 MiB，两端 checksum 必然不等）。*/
	if (done == 0) {
		done = (mode == 0) ? span : total;   /* 宿主没报长度时的兜底 */
		printf("BIGXFER_SRV: warning: no length received, assuming %llu\n", done);
	}
	if (done > total) done = total;
	t0 = now_s();
	fin.magic = MAGIC_FIN;
	fin.checksum = cksum(buf, (size_t)done);
	fin.seconds_us = (unsigned long long)((now_s() - t0) * 1e6);
	fin.span = done;
	fin.win_bad = win_bad;
	fin.win_first_bad = win_first_bad;
	if (scif_send(c, &fin, sizeof(fin), SCIF_SEND_BLOCK) != (int)sizeof(fin)) {
		perror("scif_send fin");
		return 1;
	}
	printf("BIGXFER_SRV: windows=%llu checksummed %llu/%llu bytes in %.2f s -> 0x%llx\n",
	       windows, done, total, (double)fin.seconds_us / 1e6, fin.checksum);
	printf("BIGXFER_SRV: window-verify bad=%llu first_bad=%llu\n",
	       win_bad, win_first_bad);
	printf("BIGXFER_SRV: done\n");
	fflush(stdout);

	scif_close(c);
	scif_close(ep);
	free(buf);
	return 0;
}
