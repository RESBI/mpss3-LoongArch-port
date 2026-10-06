/* Host side (length sweep): run the card's 7 transfer cases and let the card verify.
 *
 * Pattern convention (must match the card): byte at absolute offset k is
 *   PAT_BASE + ((k*3 + 1) & 0xff)
 * so the same formula holds on both sides regardless of where a transfer starts.
 *
 * build: gcc -O1 -o sw_cli sw_cli.c -I<stage>/include -L<stage>/lib64 -lscif
 * run:   sudo ./sw_cli
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
	unsigned long long dst_off;
	unsigned long long len;
};

int main(void)
{
	scif_epd_t ep;
	struct scif_portID peer;
	char *buf;
	off_t hoff;
	struct hdr_t theirs;
	struct tcase_t tc[NCASES];
	struct hdr_t ack;
	int rc, i, k;

	memset(&peer, 0, sizeof(peer));
	peer.node = 1;
	peer.port = PORT;

	buf = memalign(16384, BUFSZ);
	if (!buf) { perror("memalign"); return 1; }
	for (i = 0; i < BUFSZ; i++)
		buf[i] = (char)(PAT_BASE + ((i * 3 + 1) & 0xff));

	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	if (scif_connect(ep, &peer) < 0) { perror("scif_connect"); return 1; }
	printf("SW_CLI: connected to card port %d\n", PORT); fflush(stdout);

	if (scif_recv(ep, &theirs, sizeof(theirs), SCIF_RECV_BLOCK) != (int)sizeof(theirs)) {
		perror("scif_recv hdr"); return 1;
	}
	printf("SW_CLI: card window off=0x%llx magic=0x%llx cases=%llu\n",
	       theirs.off, theirs.magic, theirs.len); fflush(stdout);
	if (theirs.magic != MAGIC_CARD) { printf("SW_CLI: bad magic\n"); return 1; }

	if (scif_recv(ep, tc, sizeof(tc), SCIF_RECV_BLOCK) != (int)sizeof(tc)) {
		perror("scif_recv cases"); return 1;
	}

	hoff = scif_register(ep, buf, BUFSZ, 0, SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
	if (hoff < 0) { perror("scif_register"); return 1; }
	printf("SW_CLI: host window off=0x%llx (buf %p)\n",
	       (unsigned long long)hoff, buf); fflush(stdout);

	for (k = 0; k < NCASES; k++) {
		errno = 0;
		rc = scif_writeto(ep, (off_t)(hoff + tc[k].dst_off),
				  (size_t)tc[k].len,
				  (off_t)(theirs.off + tc[k].dst_off), 0);
		printf("SW_CLI: case %d dst_off=%6llu len=%6llu -> writeto %d %s\n",
		       k, tc[k].dst_off, tc[k].len, rc, rc < 0 ? strerror(errno) : "OK");
		fflush(stdout);
		if (rc < 0) break;

		if (scif_recv(ep, &ack, sizeof(ack), SCIF_RECV_BLOCK) != (int)sizeof(ack)) {
			printf("SW_CLI: ack recv failed at case %d\n", k);
			break;
		}
		printf("SW_CLI: case %d card says bad=%llu\n", k, ack.len);
		fflush(stdout);
	}

	scif_close(ep);
	printf("SW_CLI: done\n");
	return 0;
}
