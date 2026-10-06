/* Host side (forward direction, 26496 bytes): write 26496 pattern bytes INTO the
 * card's registered window with scif_writeto.
 *
 * Pattern must match the card: byte i = 0xA5 + (i & 0xff)
 *
 * build: gcc -O1 -o fw_cli fw_cli.c -I<stage>/include -L<stage>/lib64 -lscif
 * run:   sudo ./fw_cli
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
	scif_epd_t ep;
	struct scif_portID peer;
	char *buf;
	off_t hoff;
	struct hdr_t theirs;
	int rc, i;

	memset(&peer, 0, sizeof(peer));
	peer.node = 1;
	peer.port = PORT;

	buf = memalign(16384, BUFSZ);
	if (!buf) { perror("memalign"); return 1; }
	memset(buf, 0, BUFSZ);
	for (i = 0; i < PAYLOAD; i++)
		buf[i] = (char)(PAT_BASE + (i & 0xff));

	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	if (scif_connect(ep, &peer) < 0) { perror("scif_connect"); return 1; }
	printf("FW_CLI: connected to card port %d\n", PORT); fflush(stdout);

	memset(&theirs, 0, sizeof(theirs));
	if (scif_recv(ep, &theirs, sizeof(theirs), SCIF_RECV_BLOCK) != (int)sizeof(theirs)) {
		perror("scif_recv"); return 1;
	}
	printf("FW_CLI: card window offset 0x%llx magic=0x%llx len=%llu\n",
	       theirs.off, theirs.magic, theirs.len); fflush(stdout);
	if (theirs.magic != MAGIC_CARD) { printf("FW_CLI: bad card magic\n"); return 1; }

	hoff = scif_register(ep, buf, BUFSZ, 0, SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
	if (hoff < 0) { perror("scif_register"); return 1; }
	printf("FW_CLI: host window offset 0x%llx (buf %p)\n",
	       (unsigned long long)hoff, buf); fflush(stdout);

	errno = 0;
	rc = scif_writeto(ep, (off_t)hoff, (size_t)PAYLOAD, (off_t)theirs.off, 0);
	printf("FW_CLI: scif_writeto(loffset=0x%llx, len=%d, roffset=0x%llx) -> %d %s\n",
	       (unsigned long long)hoff, PAYLOAD, theirs.off, rc,
	       rc < 0 ? strerror(errno) : "OK");
	fflush(stdout);

	sleep(10);
	scif_close(ep);
	printf("FW_CLI: done\n");
	return rc < 0 ? 1 : 0;
}
