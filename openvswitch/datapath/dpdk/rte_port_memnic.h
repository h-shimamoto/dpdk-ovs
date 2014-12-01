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

#ifndef __INCLUDE_RTE_PORT_MEMNIC_H__
#define __INCLUDE_RTE_PORT_MEMNIC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rte_port.h>

#include "memnic.h"

struct rte_port_memnic_common_params;

typedef int (*rte_port_memnic_alloc_t)(struct rte_port_memnic_common_params *p);
typedef int (*rte_port_memnic_free_t)(struct rte_port_memnic_common_params *p);

struct rte_port_memnic_device {
	struct memnic_area *memnic;
	struct rte_mempool *mp;
	int up_idx, down_idx;
	uint32_t framesz;
};

struct rte_port_memnic_common_params {
	const char *name;
	rte_port_memnic_alloc_t f_alloc;
	rte_port_memnic_free_t f_free;
	struct rte_port_memnic_device *mdev;
};

struct rte_port_memnic_reader_params {
	struct rte_port_memnic_common_params param;
};

extern struct rte_port_in_ops rte_port_memnic_reader_ops;

struct rte_port_memnic_writer_params {
	struct rte_port_memnic_common_params param;
	struct rte_mbuf *tx_buf[RTE_PORT_IN_BURST_SIZE_MAX];
	uint32_t tx_buf_count;
};

extern struct rte_port_out_ops rte_port_memnic_writer_ops;

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_RTE_PORT_MEMNIC_H__ */
