/* Card side (sweep): receive N transfers of varying length/offset and verify each.
 *
 * Protocol with the host: the host sends a header per transfer describing
 * (loffset-in-its-own-window, len, dst-offset-in-card-window) and then issues
 * scif_writeto. We verify the bytes that arrived at each destination range.
 *
 * Purpose: pin down whether the observed 8192-byte truncation is absolute
 * (address mapping) or per-transfer (chunking limit), and its exact value.
 *
 * build: k1om-cc -O1 -o sw_srv sw_srv.c -lscif
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <scif.h>

#define PORT     20004
#define BUFSZ    (256 * 1024)
#define NCASES   7
#define PAT_BASE 0x3C
#define MAGIC_CARD 0x5EEDCAFEULL

struct hdr_t {
	unsigned long long off;
	unsigned long long magic;
	unsigned long long len;
};

struct tcase_t {
	unsigned long long dst_off;   /* offset inside the card window */
	unsigned long long len;
};

int main(void)
{
	scif_epd_t ep, c;
	struct scif_portID peer;
	char *buf;
	off_t myoff;
	struct hdr_t mine;
	struct tcase_t tc[NCASES];
	int rc, i, k, n, bad;

	tc[0].dst_off = 0;     tc[0].len = 4096;
	tc[1].dst_off = 4096;  tc[1].len = 4096;
	tc[2].dst_off = 0;     tc[2].len = 8192;
	tc[3].dst_off = 0;     tc[3].len = 8193;
	tc[4].dst_off = 0;     tc[4].len = 12288;
	tc[5].dst_off = 0;     tc[5].len = 16384;
	tc[6].dst_off = 0;     tc[6].len = 26496;

	buf = memalign(4096, BUFSZ);
	if (!buf) { perror("memalign"); return 1; }
	memset(buf, 0, BUFSZ);

	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	if (scif_bind(ep, PORT) < 0) { perror("scif_bind"); return 1; }
	if (scif_listen(ep, 4) < 0) { perror("scif_listen"); return 1; }
	printf("SW_SRV: listening on port %d\n", PORT); fflush(stdout);

	c = (scif_epd_t)-1;
	rc = scif_accept(ep, &peer, &c, SCIF_ACCEPT_SYNC);
	if (rc != 0) { perror("scif_accept"); return 1; }
	printf("SW_SRV: accepted from node %u port %u\n",
	       (unsigned)peer.node, (unsigned)peer.port); fflush(stdout);

	myoff = scif_register(c, buf, BUFSZ, 0, SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
	if (myoff < 0) { perror("scif_register"); return 1; }
	printf("SW_SRV: window offset 0x%llx (buf %p)\n",
	       (unsigned long long)myoff, buf); fflush(stdout);

	mine.off = (unsigned long long)myoff;
	mine.magic = MAGIC_CARD;
	mine.len = NCASES;
	if (scif_send(c, &mine, sizeof(mine), SCIF_SEND_BLOCK) != (int)sizeof(mine)) {
		perror("scif_send"); return 1;
	}
	/* also send the case table so the host knows what to do */
	if (scif_send(c, tc, sizeof(tc), SCIF_SEND_BLOCK) != (int)sizeof(tc)) {
		perror("scif_send tc"); return 1;
	}
	printf("SW_SRV: sent %d cases, waiting for transfers...\n", NCASES); fflush(stdout);

	for (k = 0; k < NCASES; k++) {
		struct hdr_t ack;
		sleep(3);

		bad = 0;
		for (i = 0; i < (int)tc[k].len; i++) {
			unsigned long long pos = tc[k].dst_off + i;
			unsigned char want = (unsigned char)(PAT_BASE + ((pos * 3 + 1) & 0xff));
			if ((unsigned char)buf[pos] != want) {
				bad++;
				if (bad <= 3)
					printf("SW_SRV: case %d (dst=%llu len=%llu): first bad at %llu want %02x got %02x\n",
					       k, tc[k].dst_off, tc[k].len, pos, want,
					       (unsigned char)buf[pos]);
			}
		}
		printf("SW_SRV: case %d: dst_off=%6llu len=%6llu -> %6d bad of %6llu %s\n",
		       k, tc[k].dst_off, tc[k].len, bad, tc[k].len,
		       bad == 0 ? "OK" : "TRUNCATED/WRONG");
		fflush(stdout);
		/* 清掉本 case 的目标区间，便于下一个 case 观察 */
		memset(buf + tc[k].dst_off, 0, tc[k].len);

		/* 告诉宿主本 case 完成 */
		ack.off = 0; ack.magic = 0xACEDULL; ack.len = (unsigned long long)bad;
		n = scif_send(c, &ack, sizeof(ack), SCIF_SEND_BLOCK);
		if (n != (int)sizeof(ack)) { printf("SW_SRV: ack send failed %d\n", n); break; }
	}

	printf("SW_SRV: sweep done\n"); fflush(stdout);
	scif_close(c);
	scif_close(ep);
	return 0;
}
