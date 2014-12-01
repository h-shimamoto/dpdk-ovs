/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2014 NEC All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <rte_config.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_atomic.h>
#include <rte_spinlock.h>
#include <rte_memcpy.h>
#include <rte_log.h>

#include "rte_port_memnic.h"

#define RTE_LOGTYPE_APP        RTE_LOGTYPE_USER1

/* TODO: will remove */
#define MEMNIC_DBG(fmt, ...) \
	RTE_LOG(DEBUG, PORT, fmt, ##__VA_ARGS__)

static int
rte_port_memnic_create(struct rte_port_memnic_common_params *conf,
		       const char *func)
{
	if (!conf) {
		RTE_LOG(ERR, PORT,
			"%s: %d: Could not create MEMNIC %s - "
			"No MEMNIC configuration\n",
			__FUNCTION__, __LINE__, func);
		return -1;
	}
	if (!conf->name) {
		RTE_LOG(ERR, PORT,
			"%s: %d: Could not create MEMNIC %s - "
			"No MEMNIC name\n",
			__FUNCTION__, __LINE__, func);
		return -1;
	}
	if (!conf->f_alloc) {
		RTE_LOG(ERR, PORT,
			"%s: %d: Could not create MEMNIC %s - "
			"No MEMNIC area allocator\n",
			__FUNCTION__, __LINE__, func);
		return -1;
	}
	if (!conf->f_free) {
		RTE_LOG(ERR, PORT,
			"%s: %d: Could not create MEMNIC %s - "
			"No MEMNIC area deallocator\n",
			__FUNCTION__, __LINE__, func);
		return -1;
	}
	if (!conf->mdev) {
		RTE_LOG(ERR, PORT,
			"%s: %d: Could not create MEMNIC %s - "
			"No MEMNIC device\n",
			__FUNCTION__, __LINE__, func);
		return -1;
	}

	if (conf->f_alloc(conf)) {
		RTE_LOG(ERR, PORT,
			"%s: %d: Could not create MEMNIC %s - "
			"MEMNIC area allocation is failed: %s\n",
			__FUNCTION__, __LINE__, func, conf->name);
		return -1;
	}

	RTE_LOG(DEBUG, PORT, "%s: SUCCESS\n", __FUNCTION__);

	return 0;
}

static int
rte_port_memnic_free(struct rte_port_memnic_common_params *conf,
		     const char *func)
{
	if (!conf) {
		RTE_LOG(ERR, PORT, "%s: %d: Could not free MEMNIC %s - "
			"No MEMNIC configuration\n",
			__FUNCTION__, __LINE__, func);
		return -1;
	}
	if (!conf->f_free) {
		RTE_LOG(ERR, PORT, "%s: %d: Could not free MEMNIC %s - "
			"No MEMNIC deallocator\n",
			__FUNCTION__, __LINE__, func);
		return -1;
	}

	return conf->f_free(conf);
}

static void *
rte_port_memnic_reader_create(void *param, __rte_unused int socket_id)
{
	struct rte_port_memnic_reader_params *conf =
		(struct rte_port_memnic_reader_params *)param;

	RTE_LOG(DEBUG, PORT, "%s: creating MEMNIC port\n", __FUNCTION__);

	if (rte_port_memnic_create(&conf->param, "reader") < 0)
		return NULL;

	return param;
}

static int
rte_port_memnic_reader_free(void *port)
{
	struct rte_port_memnic_reader_params *conf =
		(struct rte_port_memnic_reader_params *)port;

	return rte_port_memnic_free(&conf->param, "reader");
}

static void
reset_memnic_device(struct rte_port_memnic_device *mdev, const char *name)
{
	struct memnic_area *memnic = mdev->memnic;
	struct memnic_header *hdr = &memnic->hdr;
	struct memnic_data *up = &memnic->up;
	struct memnic_data *down = &memnic->down;
	int i;

	RTE_LOG(INFO, PORT, "%s: MEMNIC %s reset\n", __FUNCTION__, name);

	/* make it invalidate */
	hdr->valid = 0;
	rte_compiler_barrier();
	for (i = 0; i < MEMNIC_NR_PACKET; i++) {
		up->packets[i].status = MEMNIC_PKT_ST_FREE;
		down->packets[i].status = MEMNIC_PKT_ST_FREE;
	}
	mdev->up_idx = mdev->down_idx = 0;
	mdev->framesz = MEMNIC_MAX_FRAME_LEN;

	if (hdr->request & MEMNIC_FEAT_FRAME_SIZE) {
		uint32_t framesz = hdr->framesz;

		/* check the frame size which is requested from the guest */
		if (framesz < MEMNIC_MAX_FRAME_LEN ||
				framesz > MEMNIC_MAX_JUMBO_FRAME_LEN) {
			RTE_LOG(WARNING, PORT,
				"%s: MEMNIC %s Invalid frame size %u\n",
				__FUNCTION__, name, framesz);
			framesz = MEMNIC_MAX_FRAME_LEN;
		}
		mdev->framesz = framesz;
		hdr->framesz = framesz;
		RTE_LOG(INFO, PORT, "%s: MEMNIC %s frame size is %u\n",
			__FUNCTION__, name, framesz);
	}
	hdr->request = 0;

	rte_compiler_barrier();
	hdr->reset = 0;
	hdr->valid = 1;
}

static int
rte_port_memnic_reader_rx(void *port, struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct rte_port_memnic_reader_params *conf =
		(struct rte_port_memnic_reader_params *)port;
	struct rte_port_memnic_device *mdev = conf->param.mdev;
	struct memnic_header *hdr = &mdev->memnic->hdr;
	struct memnic_data *down = &mdev->memnic->down;
	struct rte_mempool *mp = mdev->mp;
	uint32_t framesz;
	uint32_t i;
	int idx;

	if (unlikely(hdr->reset))
		reset_memnic_device(mdev, conf->param.name);

	if (unlikely(!hdr->valid))
		return 0;

	framesz = mdev->framesz;
	idx = mdev->down_idx;
	i = 0;
	while (i < n_pkts) {
		struct memnic_packet *p = &down->packets[idx];
		struct rte_mbuf *mbuf;

		if (unlikely(p->status != MEMNIC_PKT_ST_FILLED))
			break;
		if (unlikely(p->len > framesz)) {
			/* drop */
			goto next;
		}

		mbuf = rte_pktmbuf_alloc(mp);
		if (unlikely(!mbuf))
			break;
		rte_memcpy(rte_pktmbuf_mtod(mbuf, void *), p->data, p->len);
		rte_pktmbuf_pkt_len(mbuf) = p->len;
		rte_pktmbuf_data_len(mbuf) = p->len;
		pkts[i] = mbuf;
		++i;
next:
		rte_compiler_barrier();
		p->status = MEMNIC_PKT_ST_FREE;
		if (unlikely(++idx >= MEMNIC_NR_PACKET))
			idx = 0;
	}
	mdev->down_idx = idx;

	MEMNIC_DBG("%s: recv %u pkts\n", __FUNCTION__, i);

	return i;
}

static void *
rte_port_memnic_writer_create(void *param, __rte_unused int socket_id)
{
	struct rte_port_memnic_writer_params *conf =
		(struct rte_port_memnic_writer_params *)param;

	RTE_LOG(DEBUG, PORT, "%s: creating MEMNIC port\n", __FUNCTION__);

	if (rte_port_memnic_create(&conf->param, "writer") < 0)
		return NULL;

	conf->tx_buf_count = 0;

	return param;
}

static int
rte_port_memnic_writer_free(void *port)
{
	struct rte_port_memnic_writer_params *conf =
		(struct rte_port_memnic_writer_params *)port;

	return rte_port_memnic_free(&conf->param, "writer");
}

static inline void
send_packet_burst(struct rte_port_memnic_writer_params *p)
{
	struct rte_port_memnic_device *mdev = p->param.mdev;
	struct memnic_header *hdr = &mdev->memnic->hdr;
	struct memnic_data *up = &mdev->memnic->up;
	struct rte_mbuf *bufs[RTE_PORT_IN_BURST_SIZE_MAX];
	struct memnic_packet *pkt;
	uint32_t framesz;
	uint32_t n_pkts;
	uint32_t n, i;
	int next, idx;
	volatile int *current_idx;

	n_pkts = 0;
	if (unlikely(!hdr->valid)) {
		/* drop everything */
		for (i = n_pkts; i < p->tx_buf_count; i++)
			rte_pktmbuf_free(p->tx_buf[i]);
		goto out;
	}

	framesz = mdev->framesz;
	/* length check */
	for (i = 0; i < p->tx_buf_count; i++) {
		struct rte_mbuf *buf = p->tx_buf[i];

		if (unlikely(rte_pktmbuf_pkt_len(buf) > framesz)) {
			rte_pktmbuf_free(buf);
			continue;
		}
		bufs[n_pkts++] = buf;
	}

	if (unlikely(n_pkts == 0))
		goto out;

	current_idx = &mdev->up_idx;

retry:
	idx = *current_idx;
	pkt = &up->packets[idx];
	if (unlikely(rte_atomic32_cmpset(&pkt->status,
			MEMNIC_PKT_ST_FREE, MEMNIC_PKT_ST_USED) == 0)) {
		/* cmpxchg failed */
		if (pkt->status == MEMNIC_PKT_ST_FILLED &&
				idx == *current_idx) {
			/* queue full */
			goto free_out;
		}
		goto retry;
	}
	if (unlikely(idx != *current_idx)) {
		/*
		 * Guest freed this and got false positive,
		 * need to recover the status and retry.
		 */
		pkt->status = MEMNIC_PKT_ST_FREE;
		goto retry;
	}

	next = idx;
	if (unlikely(++next >= MEMNIC_NR_PACKET))
		next = 0;
	for (i = 1; i < n_pkts; i++) {
		pkt = &up->packets[next];

		if (pkt->status != MEMNIC_PKT_ST_FREE)
			break;
		pkt->status = MEMNIC_PKT_ST_USED;
		if (unlikely(++next >= MEMNIC_NR_PACKET))
			next = 0;
	}

	mdev->up_idx = next;
	n = i;
	for (i = 0; i < n; i++) {
		struct rte_mbuf *buf = bufs[i];

		pkt = &up->packets[idx];
		pkt->len = rte_pktmbuf_pkt_len(buf);
		rte_memcpy(pkt->data, rte_pktmbuf_mtod(buf, void *), pkt->len);
		/* x86 keeps store ordering */
		rte_compiler_barrier();
		pkt->status = MEMNIC_PKT_ST_FILLED;
		if (unlikely(++idx >= MEMNIC_NR_PACKET))
			idx = 0;
	}

	MEMNIC_DBG("%s: xmit %u pkts\n", __FUNCTION__, n);

free_out:
	for (i = 0; i < n_pkts; i++)
		rte_pktmbuf_free(bufs[i]);
out:
	p->tx_buf_count = 0;
}

static inline void
send_packet(struct rte_port_memnic_writer_params *p, struct rte_mbuf *pkt)
{
	p->tx_buf[p->tx_buf_count++] = pkt;
	if (p->tx_buf_count >= RTE_PORT_IN_BURST_SIZE_MAX)
		send_packet_burst(p);
}

static int
rte_port_memnic_writer_tx(void *port, struct rte_mbuf *pkt)
{
	struct rte_port_memnic_writer_params *p =
		(struct rte_port_memnic_writer_params *)port;

	send_packet(p, pkt);

	return 0;
}

static int
rte_port_memnic_writer_tx_bulk(void *port,
			       struct rte_mbuf **pkts,
			       uint64_t pkts_mask)
{
	struct rte_port_memnic_writer_params *p =
		(struct rte_port_memnic_writer_params *)port;

	if ((pkts_mask & (pkts_mask + 1)) == 0) {
		uint32_t i, n_pkts = __builtin_popcountll(pkts_mask);

		for (i = 0; i < n_pkts; i++)
			send_packet(p, pkts[i]);
	} else {
		while (pkts_mask) {
			uint32_t pkt_idx = __builtin_ctzll(pkts_mask);

			send_packet(p, pkts[pkt_idx]);

			pkts_mask &= ~(1LLU << pkt_idx);
		}
	}
	return 0;
}

static int
rte_port_memnic_writer_flush(void *port)
{
	struct rte_port_memnic_writer_params *p =
		(struct rte_port_memnic_writer_params *)port;

	if (p->tx_buf_count > 0)
		send_packet_burst(p);

	return 0;
}

struct rte_port_in_ops rte_port_memnic_reader_ops = {
	.f_create = rte_port_memnic_reader_create,
	.f_free = rte_port_memnic_reader_free,
	.f_rx = rte_port_memnic_reader_rx,
};

struct rte_port_out_ops rte_port_memnic_writer_ops = {
	.f_create = rte_port_memnic_writer_create,
	.f_free = rte_port_memnic_writer_free,
	.f_tx = rte_port_memnic_writer_tx,
	.f_tx_bulk = rte_port_memnic_writer_tx_bulk,
	.f_flush = rte_port_memnic_writer_flush,
};
