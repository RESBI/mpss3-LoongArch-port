/* 卡端：SCIF 消息通道测试（send/recv/fence），与宿主 sc_cli 配对。
 *
 * 覆盖：
 *   1. 建链：open / bind / listen / accept
 *   2. 小消息往返（64 字节）
 *   3. 大消息往返（26496 字节 —— 与 COI 创建命令同尺寸，曾在此暴露截断问题）
 *   4. 反向大消息（卡先发、宿主回显）
 *   5. fence（顺序语义）
 *
 * 构建：k1om-cxx/k1om-cc -O2 -o sc_srv sc_srv.c -lscif
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
				printf("SC_SRV: 第 %llu 字节不符：期望 %02x 实际 %02x\n",
				       i, pat(i + seq * 1000003ULL), b[i]);
		}
	return (int)bad;
}

int main(void)
{
	scif_epd_t ep, c;
	struct scif_portID peer;
	unsigned char *buf;
	struct msg_t m, r;
	int rc, i;

	buf = malloc(BIG_LEN);
	if (!buf) { perror("malloc"); return 1; }

	ep = scif_open();
	if (ep == (scif_epd_t)-1) { perror("scif_open"); return 1; }
	if (scif_bind(ep, PORT) < 0) { perror("scif_bind"); return 1; }
	if (scif_listen(ep, 4) < 0) { perror("scif_listen"); return 1; }
	printf("SC_SRV: 监听端口 %d\n", PORT); fflush(stdout);

	c = (scif_epd_t)-1;
	if (scif_accept(ep, &peer, &c, SCIF_ACCEPT_SYNC) != 0) { perror("scif_accept"); return 1; }
	printf("SC_SRV: 已接受来自 node %u port %u 的连接\n",
	       (unsigned)peer.node, (unsigned)peer.port); fflush(stdout);

	/* 1) 小消息：卡先收 64 字节再回 64 字节 */
	memset(buf, 0, SMALL_LEN);
	rc = scif_recv(c, buf, SMALL_LEN, SCIF_RECV_BLOCK);
	if (rc != SMALL_LEN) { printf("SC_SRV: 小消息收失败 rc=%d errno=%d\n", rc, errno); return 1; }
	printf("SC_SRV: 小消息收到 %d 字节，不符 %d 个\n", rc, check_buf(buf, SMALL_LEN, 1));
	for (i = 0; i < SMALL_LEN; i++) buf[i] = pat((unsigned long long)i + 2 * 1000003ULL);
	if (scif_send(c, buf, SMALL_LEN, SCIF_SEND_BLOCK) != SMALL_LEN) { perror("scif_send small"); return 1; }

	/* 2) 大消息：卡收 26496 再回 26496 */
	memset(buf, 0, BIG_LEN);
	rc = scif_recv(c, buf, BIG_LEN, SCIF_RECV_BLOCK);
	if (rc != BIG_LEN) { printf("SC_SRV: 大消息收失败 rc=%d（期望 %d）\n", rc, BIG_LEN); return 1; }
	printf("SC_SRV: 大消息收到 %d 字节，不符 %d 个\n", rc, check_buf(buf, BIG_LEN, 3));
	for (i = 0; i < BIG_LEN; i++) buf[i] = pat((unsigned long long)i + 4 * 1000003ULL);
	if (scif_send(c, buf, BIG_LEN, SCIF_SEND_BLOCK) != BIG_LEN) { perror("scif_send big"); return 1; }

	/* 3) 卡主动发一条，等宿主回显 */
	m.magic = 0x51525354ULL; m.len = SMALL_LEN; m.seq = 5;
	if (scif_send(c, &m, sizeof(m), SCIF_SEND_BLOCK) != (int)sizeof(m)) { perror("scif_send hdr"); return 1; }
	memset(buf, 0, SMALL_LEN);
	for (i = 0; i < SMALL_LEN; i++) buf[i] = pat((unsigned long long)i + 5 * 1000003ULL);
	if (scif_send(c, buf, SMALL_LEN, SCIF_SEND_BLOCK) != SMALL_LEN) { perror("scif_send own"); return 1; }

	rc = scif_recv(c, &r, sizeof(r), SCIF_RECV_BLOCK);
	if (rc != (int)sizeof(r)) { printf("SC_SRV: 回显头收失败 rc=%d\n", rc); return 1; }
	memset(buf, 0, SMALL_LEN);
	rc = scif_recv(c, buf, SMALL_LEN, SCIF_RECV_BLOCK);
	if (rc != SMALL_LEN) { printf("SC_SRV: 回显体收失败 rc=%d\n", rc); return 1; }
	printf("SC_SRV: 回显 seq=%llu，不符 %d 个\n",
	       (unsigned long long)r.seq, check_buf(buf, SMALL_LEN, r.seq));

	/* 4) fence：确保此前消息对宿主可见的顺序语义 */
	scif_fence_signal(c, 0, 0, 0, 0, SCIF_FENCE_INIT_SELF | SCIF_SIGNAL_REMOTE);
	printf("SC_SRV: fence 已发出\n");

	scif_close(c);
	scif_close(ep);
	printf("SC_SRV: 完成\n");
	free(buf);
	return 0;
}
