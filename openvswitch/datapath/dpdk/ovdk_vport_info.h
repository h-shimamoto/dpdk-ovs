/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
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
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#ifndef __OVDK_VPORT_INFO_H_
#define __OVDK_VPORT_INFO_H_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <rte_mbuf.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_pipeline.h>
#include <rte_port_ethdev.h>
#include <rte_port_source_sink.h>
#include <rte_kni.h>

#include "rte_port_vhost.h"
#include "rte_port_ivshm.h"
#include "rte_port_veth.h"
#include "rte_port_memnic.h"

#include "ovdk_vport_types.h"
#include "ovdk_vport_states.h"

#define MZ_VPORT_INFO           "OVS_vport_info"
#define rpl_strnlen strnlen

struct vport_kni_fifo_names {
	char tx[RTE_MEMZONE_NAMESIZE];
	char rx[RTE_MEMZONE_NAMESIZE];
	char alloc[RTE_MEMZONE_NAMESIZE];
	char free[RTE_MEMZONE_NAMESIZE];
	char req[RTE_MEMZONE_NAMESIZE];
	char resp[RTE_MEMZONE_NAMESIZE];
	char sync[RTE_MEMZONE_NAMESIZE];
};

struct vport_client_ring_names {
	char rx[RTE_RING_NAMESIZE];
	char tx[RTE_RING_NAMESIZE];
	char free[RTE_RING_NAMESIZE];
	char alloc[RTE_RING_NAMESIZE];
};

struct vport_phy {
	struct rte_port_ethdev_writer_params port_writer_ethdev_params[RTE_MAX_LCORE];
	struct rte_port_ethdev_reader_params port_reader_ethdev_params;
};

struct vport_client {
	struct vport_client_ring_names ring_names;
	struct rte_port_ivshm_writer_params port_writer_client_params;
	struct rte_port_ivshm_reader_params port_reader_client_params;
};

struct vport_kni {
	struct vport_kni_fifo_names fifo_names;
	uint8_t index;
};

struct vport_veth {
	struct rte_port_veth_writer_params port_writer_veth_params[RTE_MAX_LCORE];
	struct rte_port_veth_reader_params port_reader_veth_params;
	struct veth_dev *dev;
	uint8_t index;
};

struct vport_vhost {
	struct rte_port_vhost_writer_params port_writer_vhost_params[RTE_MAX_LCORE];
	struct rte_port_vhost_reader_params port_reader_vhost_params;
	struct virtio_net *dev;
	uint8_t index;
};

struct vport_bridge {
	struct rte_port_source_params port_reader_source_params;
};

struct vport_memnic {
	struct rte_port_memnic_writer_params port_writer_memnic_params[RTE_MAX_LCORE];
	struct rte_port_memnic_reader_params port_reader_memnic_params;
	struct rte_port_memnic_device mdev;
};

struct vport_info {
	enum ovdk_vport_type __rte_cache_aligned type;
	enum ovdk_vport_state __rte_cache_aligned state;
	char __rte_cache_aligned name[OVDK_MAX_VPORT_NAMESIZE];
	uint32_t vportid;
	union {
		struct vport_phy phy;
		struct vport_client client;
		struct vport_kni kni;
		struct vport_veth veth;
		struct vport_vhost vhost;
		struct vport_bridge bridge;
		struct vport_memnic memnic;
	};
	struct rte_pipeline_port_in_params port_in_params;
	struct rte_pipeline_port_out_params port_out_params[RTE_MAX_LCORE];
	/*
	 * 'port_in_id' is the id assigned when this vport is added as an
	 * in-port to an rte_pipeline
	 */
	uint32_t port_in_id;
	/*
	 * 'port_out_id' is the id assigned when this vport is added as an
	 * out-port to an rte_pipeline. As a vport gets added to every
	 * rte_pipeline, each rte_pipeline may assign a different port id.
	 * Therefore we need an array
	 */
	uint32_t port_out_id[RTE_MAX_LCORE];
};

/*
 * Check if port_name is a vport valid name.
 *
 * A valid port_name is an alphanumeric string whose length is not greater
 * than max vport name length.
 *
 * If port_name is a valid vport name, returns 0. Otherwise, returns -1.
 */
static inline int
vport_is_valid_name(const char *port_name)
{
	size_t length = 0;
	unsigned i = 0;

	if (port_name == NULL)
		return -EINVAL;

	length = strnlen(port_name, OVDK_MAX_VPORT_NAMESIZE + 1);

	/*
	 * If port_name length is equal or greater than VPORT_INFO_NAMESZ+1
	 * strlen will return VPORT_INFO_NAMESZ+1. In that case we know
	 * port_name is too big for a valid port name. strlen does take into
	 * account the final \0 byte but it does not return it as part of the
	 * string length.
	 */
	if (length == OVDK_MAX_VPORT_NAMESIZE + 1)
		return -EINVAL;

	for (i = 0; i < length; i++)
		if (!isalnum(port_name[i]))
			return -EINVAL;

	return 0;
}

#endif /* __OVDK_VPORT_INFO_H_ */
