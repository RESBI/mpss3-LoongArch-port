/* Host side (T8): send a large block (default 4 GiB) to the card and check that
 * it arrived intact, measuring the transfer bandwidth on the way.
 *
 * Every quantity that goes near the driver is deliberately kept inside the range
 * this port has already exercised:
 *   - the host's registered window is `win` (default 1 MiB; T4 used 256 KiB);
 *   - one scif_writeto call never exceeds `rma` bytes (default 26496, the length
 *     T4 sweeps and the size of COI's own process-creation command).
 * Measured: a 4 MiB single RMA into a 64 MiB registered window fails and then
 * wedges the process inside the driver's window-reclaim path (D state,
 * unkillable, needs a reboot) - hence the small-step discipline here. The total
 * is still 4 GiB; it simply crosses the wire in many small pieces.
 *
 * Modes (the card picks, and says so in its hello message):
 *   mode 0 "direct"   - the card registered its whole buffer (small totals
 *                       only), so pieces go straight to their final offsets.
 *   mode 1 "windowed" - the usual case: the host fills the card's window with
 *                       small pieces, then tells the card "move these N bytes to
 *                       offset X" and waits for the ack before reusing it.
 *
 * build: gcc -O2 -o bigxfer_cli bigxfer_cli.c -I<stage>/include -L<stage>/lib64 -lscif
 * run:   LD_LIBRARY_PATH=<stage>/lib64 ./bigxfer_cli [total] [window] [rma]
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
#define WIN_DEFAULT (1ULL * 1024 * 1024)          /* 1 MiB registered window */
#define RMA_DEFAULT 26496ULL                      /* one writeto, T4-proven */
#define PROGRESS_EVERY 128                        /* pieces between progress lines */

struct hello_t {
	unsigned long long magic;
	unsigned long long mode;
	unsigned long long total;
	unsigned long long span;
	unsigned long long reg_off;
	unsigned long long window;
};

struct msg_t {
	unsigned long long magic;
	unsigned long long kind;
	unsigned long long a;
	unsigned long long b;
	unsigned long long c;      /* 本窗口数据的 checksum，卡端逐窗口比对 */
};

struct fin_t {
	unsigned long long magic;
	unsigned long long checksum;
	unsigned long long seconds_us;
	unsigned long long span;
	unsigned long long win_bad;
	unsigned long long win_first_bad;
};

/* splitmix64 */
static unsigned long long rnd_state;
static unsigned long long next_rand(void)
{
	unsigned long long z = (rnd_state += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

/* Identical to the card's cksum(). */
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

/* scif_register pins whole pages: address and length must line up with the host
 * page size (16 KiB on the machine this was written for), else it answers
 * EINVAL. Do not hard-code 4096 here. */
static size_t page_size(void)
{
	long p = sysconf(_SC_PAGESIZE);
	return (p > 0) ? (size_t)p : 4096;
}

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	unsigned long long total = BIG_DEFAULT, win = WIN_DEFAULT, rma = RMA_DEFAULT;
	unsigned long long sent = 0, off, pieces = 0;
	scif_epd_t ep;
	struct scif_portID peer;
	unsigned char *buf, *wbuf;
	off_t hoff;
	struct hello_t hello;
	struct msg_t m;
	struct fin_t fin;
	double t_fill, t_ck, t_xfer, t0, t_start, t_wire = 0;
	unsigned long long host_ck;

	setvbuf(stdout, NULL, _IOLBF, 0);   /* a hang must not swallow progress */
	if (argc > 1) total = strtoull(argv[1], NULL, 0);
	if (argc > 2) win = strtoull(argv[2], NULL, 0);
	if (argc > 3) rma = strtoull(argv[3], NULL, 0);
	if (total == 0 || (total & 7)) {
		printf("BIGXFER: bad total %llu\n", total);
		return 2;
	}
	if (win == 0) win = WIN_DEFAULT;
	if (rma == 0 || rma > win) rma = (win < RMA_DEFAULT) ? win : RMA_DEFAULT;
	if (win > total) win = total;
	{
		size_t pg = page_size();
		unsigned long long rounded = ((win + pg - 1) / pg) * pg;
		if (rounded != win) {
			printf("BIGXFER: window %llu is not a multiple of the %lu-byte page, using %llu\n",
			       win, (unsigned long)pg, rounded);
			win = rounded;
			if (win > total) win = total;
		}
	}

	printf("BIGXFER: total=%llu window=%llu rma=%llu (host page %lu)\n",
	       total, win, rma, (unsigned long)page_size());

	/* 1. deterministic pattern + checksum (both timed, neither is the transfer) */
	t0 = now_s();
	buf = memalign(page_size(), (size_t)total);
	if (!buf) {
		printf("BIGXFER: malloc %llu FAILED (%s)\n", total, strerror(errno));
		return 3;
	}
	rnd_state = 0x0123456789ABCDEFULL;
	{
		size_t i;
		for (i = 0; i + 8 <= (size_t)total; i += 8) {
			unsigned long long w = next_rand();
			memcpy(buf + i, &w, 8);
		}
	}
	t_fill = now_s() - t0;
	t0 = now_s();
	host_ck = cksum(buf, (size_t)total);
	t_ck = now_s() - t0;
	printf("BIGXFER: filled %llu bytes in %.2f s, host checksum 0x%llx (in %.2f s)\n",
	       total, t_fill, host_ck, t_ck);

	/* 2. connect (the card-side server needs a moment after ssh starts it) */
	memset(&peer, 0, sizeof(peer));
	peer.node = 1;
	peer.port = PORT;
	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	{
		int tries;
		for (tries = 1; ; tries++) {
			errno = 0;
			if (scif_connect(ep, &peer) == 0) break;
			/* On this stack the first call can come back non-zero while the
			 * connection is in fact established; a second call then answers
			 * EISCONN. Treat that as success rather than retrying forever. */
			if (errno == EISCONN) {
				printf("BIGXFER: connect reported EISCONN, treating as connected\n");
				break;
			}
			if (tries >= 60) { perror("scif_connect"); return 1; }
			if (tries == 1)
				printf("BIGXFER: waiting for the card-side server (port %d)...\n", PORT);
			sleep(1);
		}
		if (tries > 1) printf("BIGXFER: connected after %d attempts\n", tries);
	}
	if (scif_recv(ep, &hello, sizeof(hello), SCIF_RECV_BLOCK) != (int)sizeof(hello)) {
		perror("scif_recv hello");
		return 1;
	}
	if (hello.magic != MAGIC_HELLO) {
		printf("BIGXFER: bad hello magic 0x%llx\n", hello.magic);
		return 1;
	}
	printf("BIGXFER: card mode=%s span=%llu window=%llu reg_off=0x%llx\n",
	       hello.mode == 0 ? "direct" : "windowed",
	       hello.span, hello.window, hello.reg_off);

	/* our own registered window: the RMA source */
	wbuf = memalign(page_size(), (size_t)win);
	if (!wbuf) { perror("memalign wbuf"); return 3; }
	memset(wbuf, 0, (size_t)win);
	hoff = scif_register(ep, wbuf, (size_t)win, 0,
			     SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
	if (hoff < 0) { perror("scif_register host window"); return 4; }
	printf("BIGXFER: host window %llu bytes at 0x%llx\n",
	       win, (unsigned long long)hoff);

	/* 3. push the data; only the writeto calls are timed */
	t_start = now_s();
	if (hello.mode == 0) {
		/* direct: every piece goes to its final offset, no handshaking */
		for (off = 0; off < total; ) {
			unsigned long long len = total - off;
			if (len > rma) len = rma;
			memcpy(wbuf, buf + off, (size_t)len);
			t0 = now_s();
			if (pieces < 4 || pieces % PROGRESS_EVERY == 0)
				printf("BIGXFER: piece %llu local_off=%llu len=%llu dst_off=0x%llx\n",
				       pieces, off, len,
				       (unsigned long long)(hello.reg_off + off));
			if (scif_writeto(ep, hoff, (size_t)len,
					 (off_t)(hello.reg_off + off), 0) < 0) {
				printf("BIGXFER: writeto FAILED at off=%llu len=%llu (%s)\n",
				       off, len, strerror(errno));
				return 5;
			}
			t_wire += now_s() - t0;
			sent += len;
			off += len;
			if (++pieces % PROGRESS_EVERY == 0)
				printf("BIGXFER: %llu/%llu bytes (%.2f s)\n",
				       sent, total, now_s() - t_start);
		}
	} else {
		/* windowed: fill the card's window piece by piece, then hand it over */
		for (off = 0; off < total; ) {
			unsigned long long filled = 0;

			while (filled < win && off + filled < total) {
				unsigned long long len = total - (off + filled);
				if (len > rma) len = rma;
				if (len > win - filled) len = win - filled;
				memcpy(wbuf + filled, buf + off + filled, (size_t)len);
				t0 = now_s();
				if (scif_writeto(ep, (off_t)(hoff + filled), (size_t)len,
						 (off_t)(hello.reg_off + filled), 0) < 0) {
					printf("BIGXFER: writeto FAILED at off=%llu len=%llu (%s)\n",
					       off + filled, len, strerror(errno));
					return 5;
				}
				t_wire += now_s() - t0;
				if (pieces < 16 || pieces % PROGRESS_EVERY == 0)
					printf("BIGXFER: piece %llu local_off=%llu len=%llu dst_off=0x%llx\n",
					       pieces, off + filled, len,
					       (unsigned long long)(hello.reg_off + filled));
				filled += len;
				pieces++;
			}
			memset(&m, 0, sizeof(m));
			m.magic = MAGIC_MSG;
			m.kind = KIND_CHUNK;
			m.a = off;          /* where this block belongs in the big buffer */
			m.b = filled;
			m.c = cksum(buf + off, (size_t)filled);   /* 逐窗口校验用 */
			if (scif_send(ep, &m, sizeof(m), SCIF_SEND_BLOCK) != (int)sizeof(m)) {
				printf("BIGXFER: chunk msg failed\n");
				return 5;
			}
			if (scif_recv(ep, &m, sizeof(m), SCIF_RECV_BLOCK) != (int)sizeof(m)) {
				printf("BIGXFER: chunk ack failed\n");
				return 5;
			}
			sent += filled;
			off += filled;
			printf("BIGXFER: %llu/%llu bytes (%.2f s)\n",
			       sent, total, now_s() - t_start);
		}
	}
	t_xfer = now_s() - t_start;

	/* 4. done, then collect the card's checksum */
	memset(&m, 0, sizeof(m));
	m.magic = MAGIC_MSG;
	m.kind = KIND_DONE;
	m.a = sent;
	if (scif_send(ep, &m, sizeof(m), SCIF_SEND_BLOCK) != (int)sizeof(m)) {
		printf("BIGXFER: done msg failed\n");
		return 5;
	}
	if (scif_recv(ep, &fin, sizeof(fin), SCIF_RECV_BLOCK) != (int)sizeof(fin)) {
		printf("BIGXFER: final message failed\n");
		return 5;
	}
	printf("BIGXFER: pieces=%llu window_verify_bad=%llu first_bad=%llu\n",
	       pieces, fin.win_bad, fin.win_first_bad);
	scif_close(ep);

	/* 5. report */
	printf("BIGXFER total=%llu sent=%llu span=%llu mode=%llu window=%llu rma=%llu\n",
	       total, sent, hello.span, hello.mode, win, rma);
	printf("BIGXFER host_checksum=0x%llx card_checksum=0x%llx card_cksum_seconds=%.2f\n",
	       host_ck, fin.checksum, (double)fin.seconds_us / 1e6);
	if (t_xfer > 0) {
		double mb = (double)sent / (1024.0 * 1024.0);
		printf("BIGXFER transfer_seconds=%.3f bandwidth_MBps=%.1f bandwidth_GiBps=%.2f\n",
		       t_xfer, mb / t_xfer, (double)sent / (1024.0 * 1024.0 * 1024.0) / t_xfer);
	}
	if (t_wire > 0) {
		double mb = (double)sent / (1024.0 * 1024.0);
		/* 只统计 scif_writeto 本身：这才是链路侧的数字；
		 * bandwidth_* 是端到端，含卡端逐窗口校验与搬运的等待。 */
		printf("BIGXFER wire_seconds=%.3f wire_bandwidth_MBps=%.1f wire_bandwidth_GiBps=%.2f\n",
		       t_wire, mb / t_wire, (double)sent / (1024.0 * 1024.0 * 1024.0) / t_wire);
	}

	if (sent != total) {
		printf("BIGXFER RESULT=PARTIAL (sent %llu of %llu)\n", sent, total);
		return 6;
	}
	if (fin.win_bad) {
		printf("BIGXFER RESULT=MISMATCH (first bad window %llu of %llu)\n",
		       fin.win_first_bad, sent / win);
		return 7;
	}
	if (fin.checksum != host_ck) {
		printf("BIGXFER RESULT=MISMATCH (window checksums all passed, whole-buffer differs)\n");
		return 7;
	}
	printf("BIGXFER RESULT=OK\n");
	return 0;
}
