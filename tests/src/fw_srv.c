/* Card side (forward direction, 26496 bytes): the host writes INTO the card's
 * registered window with scif_writeto. We then verify every byte.
 *
 * Purpose: the earlier forward test used only 4096 bytes and passed. The reverse
 * test truncated at exactly 8192 bytes. This run tells us whether the 8192 limit
 * is direction-specific or a general transfer-size limit.
 *
 * build: k1om-cc -O1 -o fw_srv fw_srv.c -lscif
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <scif.h>

#define PORT      20003
#define BUFSZ     (128 * 1024)
#define PAYLOAD   26496
#define PAT_BASE  0xA5
#define MAGIC_CARD 0xF0F0C0DEULL

struct hdr_t {
	unsigned long long off;
	unsigned long long magic;
	unsigned long long len;
};

int main(void)
{
	scif_epd_t ep, c;
	struct scif_portID peer;
	char *buf;
	off_t myoff;
	struct hdr_t mine;
	int rc, i, bad = 0, n;

	buf = memalign(4096, BUFSZ);
	if (!buf) { perror("memalign"); return 1; }
	memset(buf, 0, BUFSZ);

	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	if (scif_bind(ep, PORT) < 0) { perror("scif_bind"); return 1; }
	if (scif_listen(ep, 4) < 0) { perror("scif_listen"); return 1; }
	printf("FW_SRV: listening on port %d\n", PORT); fflush(stdout);

	c = (scif_epd_t)-1;
	rc = scif_accept(ep, &peer, &c, SCIF_ACCEPT_SYNC);
	if (rc != 0) { perror("scif_accept"); return 1; }
	printf("FW_SRV: accepted from node %u port %u\n",
	       (unsigned)peer.node, (unsigned)peer.port); fflush(stdout);

	myoff = scif_register(c, buf, BUFSZ, 0, SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
	if (myoff < 0) { perror("scif_register"); return 1; }
	printf("FW_SRV: local window offset 0x%llx (buf %p)\n",
	       (unsigned long long)myoff, buf); fflush(stdout);

	mine.off = (unsigned long long)myoff;
	mine.magic = MAGIC_CARD;
	mine.len = PAYLOAD;
	n = scif_send(c, &mine, sizeof(mine), SCIF_SEND_BLOCK);
	printf("FW_SRV: sent own header (%d bytes), waiting for host writeto...\n", n);
	fflush(stdout);

	sleep(8);

	printf("FW_SRV: first 64 bytes of buffer:\n");
	for (i = 0; i < 4; i++) {
		int j;
		printf("  [%5d] ", i * 16);
		for (j = 0; j < 16; j++)
			printf("%02x ", (unsigned char)buf[i * 16 + j]);
		printf("\n");
	}
	for (i = 0; i < PAYLOAD; i++) {
		unsigned char want = (unsigned char)(PAT_BASE + (i & 0xff));
		if ((unsigned char)buf[i] != want) {
			bad++;
			if (bad <= 6)
				printf("FW_SRV: byte %d mismatch: want %02x got %02x\n",
				       i, want, (unsigned char)buf[i]);
		}
	}
	printf("FW_SRV: forward check: %d bytes, %d mismatches -> %s\n",
	       PAYLOAD, bad,
	       bad == 0 ? "HOST->CARD 26496 BYTES CORRECT"
			: "TRUNCATED OR WRONG (general transfer limit suspected)");
	fflush(stdout);

	scif_close(c);
	scif_close(ep);
	return 0;
}
