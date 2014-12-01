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

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_random.h>
#include <rte_ether.h>
#include <rte_log.h>

#include "ovdk_vport_info.h"
#include "ovdk_stats.h"
#include "ovdk_mempools.h"

#include "ovdk_vport_memnic.h"
#include "rte_port_memnic.h"

#define PORT_MEMNIC_RX_BURST_SIZE	(32)
#define PORT_MEMNIC_TX_BURST_SIZE	(32)

static int
ovdk_vport_memnic_init(struct rte_port_memnic_device *mdev, const char *name)
{
	struct memnic_area *memnic;
	struct memnic_header *hdr;
	int fd, ret = -1;

	fd = shm_open(name, O_RDWR|O_CREAT, 0660);
	if (fd < 0) {
		RTE_LOG(WARNING, USER1,
			"MEMNIC: shm_open error %s\n", name);
		return -1;
	}

	if (ftruncate(fd, MEMNIC_AREA_SIZE) < 0) {
		RTE_LOG(WARNING, USER1,
			"MEMNIC: ftruncate error %s\n", name);
		goto out;
	}

	memnic = mmap(NULL, MEMNIC_AREA_SIZE,
			PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (memnic == MAP_FAILED) {
		RTE_LOG(WARNING, USER1,
			"MEMNIC: mmap error %s\n", name);
		goto out;
	}

	/* initialize MEMNIC area */
	hdr = &memnic->hdr;
	if (hdr->magic != MEMNIC_MAGIC || hdr->version != MEMNIC_VERSION) {
		uint64_t rand;

		/* set MAGIC and VERSION */
		hdr->magic = MEMNIC_MAGIC;
		hdr->version = MEMNIC_VERSION;

		/* provide variable frame size feature */
		hdr->features = MEMNIC_FEAT_ALL;
		hdr->request = 0;

		/* random MAC address generation */
		rand = rte_rand();
		rte_memcpy(hdr->mac_addr, &rand, ETHER_ADDR_LEN);
		hdr->mac_addr[0] &= ~ETHER_GROUP_ADDR;		/* clear multicast bit */
		hdr->mac_addr[0] |= ETHER_LOCAL_ADMIN_ADDR;	/* set local assignment bit */
		RTE_LOG(INFO, USER1,
			"MEMNIC %s: Generate MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
			name,
			hdr->mac_addr[0], hdr->mac_addr[1],
			hdr->mac_addr[2], hdr->mac_addr[3],
			hdr->mac_addr[4], hdr->mac_addr[5]);
	}
	mdev->memnic = memnic;
	mdev->up_idx = mdev->down_idx = 0;
	mdev->framesz = MEMNIC_MAX_FRAME_LEN;

	ret = 0;

out:
	close(fd);

	return ret;
}

static int
ovdk_vport_memnic_alloc(struct rte_port_memnic_common_params *p)
{
	struct rte_port_memnic_device *mdev = p->mdev;

	/* try to attach */
	if (!mdev->memnic && ovdk_vport_memnic_init(mdev, p->name) < 0) {
		/* error */
		return -1;
	}

	return 0;
}

static int
ovdk_vport_memnic_free(__rte_unused struct rte_port_memnic_common_params *p)
{
	return 0;
}

int ovdk_vport_memnic_port_init(struct vport_info *vport_info)
{
	struct rte_port_memnic_reader_params *port_reader_params;
	struct rte_port_memnic_writer_params *port_writer_params;
	struct rte_pipeline_port_in_params *port_in_params;
	struct rte_pipeline_port_out_params *port_out_params;
	int i;

	if (!vport_info)
		rte_panic("Cannot init MEMNIC port, invalid vport info\n");

	RTE_LOG(INFO, USER1,
		"Initializing MEMNIC port %u\n", vport_info->vportid);

	vport_info->memnic.mdev.mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);

	port_reader_params = &vport_info->memnic.port_reader_memnic_params;

	port_reader_params->param.name = vport_info->name;
	port_reader_params->param.f_alloc = ovdk_vport_memnic_alloc;
	port_reader_params->param.f_free = ovdk_vport_memnic_free;
	port_reader_params->param.mdev = &vport_info->memnic.mdev;

	vport_info->type = OVDK_VPORT_TYPE_MEMNIC;

	/* setup port_in_params and port_out_params */
	port_in_params = &vport_info->port_in_params;
	port_in_params->ops = &rte_port_memnic_reader_ops;
	port_in_params->arg_create = port_reader_params;
	port_in_params->f_action = NULL;
	port_in_params->arg_ah = &vport_info->vportid;
	port_in_params->burst_size = PORT_MEMNIC_RX_BURST_SIZE;

	for (i = 0; i < RTE_MAX_LCORE; i++) {
		port_writer_params = &vport_info->memnic.port_writer_memnic_params[i];
		port_writer_params->param.name = vport_info->name;
		port_writer_params->param.f_alloc = ovdk_vport_memnic_alloc;
		port_writer_params->param.f_free = ovdk_vport_memnic_free;
		port_writer_params->param.mdev = &vport_info->memnic.mdev;

		port_out_params = &vport_info->port_out_params[i];
		port_out_params->ops = &rte_port_memnic_writer_ops;
		port_out_params->arg_create = port_writer_params;
		port_out_params->f_action = ovdk_stats_port_out_update;
		port_out_params->f_action_bulk = ovdk_stats_port_out_update_bulk;
		port_out_params->arg_ah = &vport_info->vportid;
	}

	return 0;
}
