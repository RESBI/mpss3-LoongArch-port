/* 宿主：SCIF 消息通道测试，与卡端 sc_srv 配对。
 * 构建：gcc -O2 -o sc_cli sc_cli.c -I<stage>/include -L<stage>/lib64 -lscif -Wl,-rpath,<stage>/lib64
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <scif.h>

#define PORT        20005
#define SMALL_LEN   64
#define BIG_LEN     26496
#define PAT_BASE    0x5A

struct msg_t {
	unsigned long long magic;
	unsigned long long len;
	unsigned long long seq;
};

static unsigned char pat(unsigned long long i)
{
	return (unsigned char)(PAT_BASE + ((i * 7 + 3) & 0xff));
}

static int check_buf(const unsigned char *b, unsigned long long len, unsigned long long seq)
{
	unsigned long long i, bad = 0;
	for (i = 0; i < len; i++)
		if (b[i] != pat(i + seq * 1000003ULL)) {
			bad++;
			if (bad <= 3)
				printf("SC_CLI: 第 %llu 字节不符：期望 %02x 实际 %02x\n",
				       i, pat(i + seq * 1000003ULL), b[i]);
		}
	return (int)bad;
}

int main(void)
{
	scif_epd_t ep;
	struct scif_portID peer;
	unsigned char *buf;
	struct msg_t m, r;
	int rc, i;

	memset(&peer, 0, sizeof(peer));
	peer.node = 1;
	peer.port = PORT;

	buf = malloc(BIG_LEN);
	if (!buf) { perror("malloc"); return 1; }

	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	if (scif_connect(ep, &peer) < 0) { perror("scif_connect"); return 1; }
	printf("SC_CLI: 已连接卡端端口 %d\n", PORT); fflush(stdout);

	/* 1) 小消息往返 */
	for (i = 0; i < SMALL_LEN; i++) buf[i] = pat((unsigned long long)i + 1 * 1000003ULL);
	if (scif_send(ep, buf, SMALL_LEN, SCIF_SEND_BLOCK) != SMALL_LEN) { perror("scif_send small"); return 1; }
	memset(buf, 0, SMALL_LEN);
	rc = scif_recv(ep, buf, SMALL_LEN, SCIF_RECV_BLOCK);
	if (rc != SMALL_LEN) { printf("SC_CLI: 小消息回包收失败 rc=%d errno=%d\n", rc, errno); return 1; }
	printf("SC_CLI: 小消息往返 %d 字节，不符 %d 个\n", rc, check_buf(buf, SMALL_LEN, 2));

	/* 2) 大消息往返（26496 = COI 创建命令尺寸）*/
	for (i = 0; i < BIG_LEN; i++) buf[i] = pat((unsigned long long)i + 3 * 1000003ULL);
	if (scif_send(ep, buf, BIG_LEN, SCIF_SEND_BLOCK) != BIG_LEN) { perror("scif_send big"); return 1; }
	memset(buf, 0, BIG_LEN);
	rc = scif_recv(ep, buf, BIG_LEN, SCIF_RECV_BLOCK);
	if (rc != BIG_LEN) { printf("SC_CLI: 大消息回包收失败 rc=%d（期望 %d）\n", rc, BIG_LEN); return 1; }
	printf("SC_CLI: 大消息往返 %d 字节，不符 %d 个\n", rc, check_buf(buf, BIG_LEN, 4));

	/* 3) 接收卡主动发来的消息并回显 */
	rc = scif_recv(ep, &m, sizeof(m), SCIF_RECV_BLOCK);
	if (rc != (int)sizeof(m)) { printf("SC_CLI: 卡端头收失败 rc=%d\n", rc); return 1; }
	printf("SC_CLI: 卡端发来 seq=%llu magic=0x%llx\n",
	       (unsigned long long)m.seq, (unsigned long long)m.magic);
	memset(buf, 0, SMALL_LEN);
	rc = scif_recv(ep, buf, SMALL_LEN, SCIF_RECV_BLOCK);
	if (rc != SMALL_LEN) { printf("SC_CLI: 卡端体收失败 rc=%d\n", rc); return 1; }
	printf("SC_CLI: 卡端消息体不符 %d 个\n", check_buf(buf, SMALL_LEN, m.seq));

	/* 回显（保持同一 seq，便于卡端用同一公式校验）*/
	r.magic = m.magic; r.len = m.len; r.seq = m.seq;
	if (scif_send(ep, &r, sizeof(r), SCIF_SEND_BLOCK) != (int)sizeof(r)) { perror("scif_send echo hdr"); return 1; }
	if (scif_send(ep, buf, SMALL_LEN, SCIF_SEND_BLOCK) != SMALL_LEN) { perror("scif_send echo body"); return 1; }

	/* 4) fence */
	scif_fence_signal(ep, 0, 0, 0, 0, SCIF_FENCE_INIT_SELF | SCIF_SIGNAL_REMOTE);
	printf("SC_CLI: fence 已发出\n");

	scif_close(ep);
	printf("SC_CLI: 完成\n");
	free(buf);
	return 0;
}
