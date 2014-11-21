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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_ethdev.h>
#include <rte_errno.h>
#include <config.h>
#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_string_fns.h>
#include <rte_memzone.h>

#include "datapath/dpdk/ovdk_stats.h"
#include "datapath/dpdk/ovdk_datapath_messages.h"
#include "command-line.h"

#define NO_FLAGS                0
#define MZ_STATS_INFO           "OVDK_stats_info"
#define VPORT_STATS_SIZE        (sizeof(struct vport_statistics) * \
                                 OVDK_MAX_VPORTS + \
                                 sizeof(struct vswitch_statistics))
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NUM_BYTES_MAC_ADDR  6
#define MAC_ADDR_STR_INT_L  3
#define NEWLINE_CHAR_OFFSET 1
#define RANDOM1 263
#define RANDOM2 737
#define RANDOM3 156
#define RANDOM4 488
#define RANDOM5 333

void test_ovdk_stats_init_vport(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_stats_init_vswitch_data(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_stats_init_vswitch_control(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_stats_port_out_update(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_ovdk_stats_vport_get(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

void
test_ovdk_stats_init_vport(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t set_vport_rx_drop = RANDOM1;
	uint64_t set_vport_rx = RANDOM2;
	uint64_t set_vport_tx_drop = RANDOM3;
	uint64_t set_vport_tx = RANDOM4;
	uint64_t set_vport_overrun = RANDOM5;
	unsigned vportid = OVDK_MAX_VPORTS - 1;

	ovdk_stats_init();
	ovdk_stats_clear();

	/*
	* Check that all stats are 0 to begin with.
	* These stats must be 0 so that our increment test works
	*/
	assert(ovdk_stats_vport_rx_drop_get(vportid) == 0);
	assert(ovdk_stats_vport_rx_get(vportid) == 0);
	assert(ovdk_stats_vport_tx_drop_get(vportid) == 0);
	assert(ovdk_stats_vport_tx_get(vportid) == 0);
	assert(ovdk_stats_vport_overrun_get(vportid) == 0);

	/* Increment each statsitic of vportid */
	ovdk_stats_vport_rx_increment(vportid, set_vport_rx);
	ovdk_stats_vport_rx_drop_increment(vportid, set_vport_rx_drop);
	ovdk_stats_vport_tx_increment(vportid, set_vport_tx);
	ovdk_stats_vport_tx_drop_increment(vportid, set_vport_tx_drop);
	ovdk_stats_vport_overrun_increment(vportid, set_vport_overrun);

	/* Check that the increment and get functions worked correctly with asserts */
	assert(ovdk_stats_vport_rx_drop_get(vportid) == set_vport_rx_drop);
	assert(ovdk_stats_vport_rx_get(vportid) == set_vport_rx);
	assert(ovdk_stats_vport_tx_drop_get(vportid) == set_vport_tx_drop);
	assert(ovdk_stats_vport_tx_get(vportid) == set_vport_tx);
	assert(ovdk_stats_vport_overrun_get(vportid) == set_vport_overrun);

	/* Reset to 0 with stats clear */
	ovdk_stats_clear();
	/* Check that stats clear function worked */
	assert(ovdk_stats_vport_rx_drop_get(vportid) == 0);
	assert(ovdk_stats_vport_rx_get(vportid) == 0);
	assert(ovdk_stats_vport_tx_drop_get(vportid) == 0);
	assert(ovdk_stats_vport_tx_get(vportid) == 0);
	assert(ovdk_stats_vport_overrun_get(vportid) == 0);
}

void
test_ovdk_stats_init_vswitch_data(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t set_vswitch_data_rx = RANDOM1;
	uint64_t set_vswitch_data_rx_drop = RANDOM2;
	uint64_t set_vswitch_data_tx = RANDOM3;
	uint64_t set_vswitch_data_tx_drop = RANDOM4;
	uint64_t set_vswitch_data_overrun = RANDOM5;

	ovdk_stats_init();
	ovdk_stats_clear();

	/*
	* Check that all stats are 0 to begin with.
	* These stats must be 0 so that our increment test works
	*/
	assert(ovdk_stats_vswitch_data_rx_get() == 0);
	assert(ovdk_stats_vswitch_data_rx_drop_get() == 0);
	assert(ovdk_stats_vswitch_data_tx_get() == 0);
	assert(ovdk_stats_vswitch_data_tx_drop_get() == 0);
	assert(ovdk_stats_vswitch_data_overrun_get() == 0);

	/* Increment each statsitic */
	ovdk_stats_vswitch_data_rx_increment(set_vswitch_data_rx);
	ovdk_stats_vswitch_data_rx_drop_increment(set_vswitch_data_rx_drop);
	ovdk_stats_vswitch_data_tx_increment(set_vswitch_data_tx);
	ovdk_stats_vswitch_data_tx_drop_increment(set_vswitch_data_tx_drop);
	ovdk_stats_vswitch_data_overrun_increment(set_vswitch_data_overrun);

	/* Check that the increment and get functions worked correctly with asserts */
	assert(ovdk_stats_vswitch_data_rx_get() == set_vswitch_data_rx);
	assert(ovdk_stats_vswitch_data_rx_drop_get() == set_vswitch_data_rx_drop);
	assert(ovdk_stats_vswitch_data_tx_get() == set_vswitch_data_tx);
	assert(ovdk_stats_vswitch_data_tx_drop_get() == set_vswitch_data_tx_drop);
	assert(ovdk_stats_vswitch_data_overrun_get() == set_vswitch_data_overrun);

	/* Reset to 0 with stats clear */
	ovdk_stats_clear();
	/* Check that stats clear function worked */
	assert(ovdk_stats_vswitch_data_rx_get() == 0);
	assert(ovdk_stats_vswitch_data_rx_drop_get() == 0);
	assert(ovdk_stats_vswitch_data_tx_get() == 0);
	assert(ovdk_stats_vswitch_data_tx_drop_get() == 0);
	assert(ovdk_stats_vswitch_data_overrun_get() == 0);
}

void
test_ovdk_stats_init_vswitch_control(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint64_t set_vswitch_control_rx = RANDOM1;
	uint64_t set_vswitch_control_rx_drop = RANDOM2;
	uint64_t set_vswitch_control_tx = RANDOM3;
	uint64_t set_vswitch_control_tx_drop = RANDOM4;
	uint64_t set_vswitch_control_overrun = RANDOM5;

	ovdk_stats_init();
	ovdk_stats_clear();

	/*
	* Check that all stats are 0 to begin with.
	* These stats must be 0 so that our increment test works
	*/
	assert(ovdk_stats_vswitch_control_rx_get() == 0);
	assert(ovdk_stats_vswitch_control_rx_drop_get() == 0);
	assert(ovdk_stats_vswitch_control_tx_get() == 0);
	assert(ovdk_stats_vswitch_control_tx_drop_get() == 0);
	assert(ovdk_stats_vswitch_control_overrun_get() == 0);

	/* Increment each statsitic */
	ovdk_stats_vswitch_control_rx_increment(set_vswitch_control_rx);
	ovdk_stats_vswitch_control_rx_drop_increment(set_vswitch_control_rx_drop);
	ovdk_stats_vswitch_control_tx_increment(set_vswitch_control_tx);
	ovdk_stats_vswitch_control_tx_drop_increment(set_vswitch_control_tx_drop);
	ovdk_stats_vswitch_control_overrun_increment(set_vswitch_control_overrun);

	/* Check that the increment and get functions worked correctly with asserts */
	assert(ovdk_stats_vswitch_control_rx_get() == set_vswitch_control_rx);
	assert(ovdk_stats_vswitch_control_rx_drop_get() == set_vswitch_control_rx_drop);
	assert(ovdk_stats_vswitch_control_tx_get() == set_vswitch_control_tx);
	assert(ovdk_stats_vswitch_control_tx_drop_get() == set_vswitch_control_tx_drop);
	assert(ovdk_stats_vswitch_control_overrun_get() == set_vswitch_control_overrun);

	/* Reset to 0 with stats clear */
	ovdk_stats_clear();
	/* Check that stats clear function worked */
	assert(ovdk_stats_vswitch_control_rx_get() == 0);
	assert(ovdk_stats_vswitch_control_rx_drop_get() == 0);
	assert(ovdk_stats_vswitch_control_tx_get() == 0);
	assert(ovdk_stats_vswitch_control_tx_drop_get() == 0);
	assert(ovdk_stats_vswitch_control_overrun_get() == 0);
}

void
test_ovdk_stats_port_out_update( int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct rte_mbuf *unused = NULL;
	uint64_t pkts_mask = 0;
	unsigned vportid = OVDK_MAX_VPORTS - 1;

	ovdk_stats_init();
	ovdk_stats_clear();
	assert(ovdk_stats_port_out_update(unused, &pkts_mask, &vportid) == 0);
	assert(ovdk_stats_vport_tx_get(vportid) == 1);

	pkts_mask = 0xF;
	assert(ovdk_stats_port_out_update_bulk(&unused, &pkts_mask, &vportid) == 0);
	/* Check that correct number of packets transmitted */
	assert(ovdk_stats_vport_tx_get(vportid) == 1 + __builtin_popcount(pkts_mask));
}

void
test_ovdk_stats_vport_get( int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct ovdk_port_stats statistics;
	struct ovdk_port_stats *stats = NULL;
	unsigned vportid = OVDK_MAX_VPORTS - 1;
	uint64_t set_vport_rx = RANDOM1;
	uint64_t set_vport_rx_drop = RANDOM2;
	uint64_t set_vport_tx = RANDOM3;
	uint64_t set_vport_tx_drop = RANDOM4;

	/* Check function returns correct error for stats = NULL */
	assert(ovdk_stats_vport_get(vportid, stats) == -1);

	stats = &statistics;
	ovdk_stats_init();
	ovdk_stats_clear();

	ovdk_stats_vport_rx_increment(vportid, set_vport_rx);
	ovdk_stats_vport_rx_drop_increment(vportid, set_vport_rx_drop);
	ovdk_stats_vport_tx_increment(vportid, set_vport_tx);
	ovdk_stats_vport_tx_drop_increment(vportid, set_vport_tx_drop);

	assert(ovdk_stats_vport_get(vportid, stats) == 0);
	assert(stats->rx == set_vport_rx);
	assert(stats->rx_drop == set_vport_rx_drop);
	assert(stats->tx == set_vport_tx);
	assert(stats->tx_drop == set_vport_tx_drop);
}

static const struct command commands[] = {
	{"stats-init-vport", 0, 0, test_ovdk_stats_init_vport},
	{"stats-init-vswitch-data", 0, 0, test_ovdk_stats_init_vswitch_data},
	{"stats-init-vswitch-control", 0, 0, test_ovdk_stats_init_vswitch_control},
	{"stats-port-out-update", 0, 0, test_ovdk_stats_port_out_update},
	{"stats-port-out-update", 0, 0, test_ovdk_stats_port_out_update},
	{"stats-vport-get", 0, 0, test_ovdk_stats_vport_get},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	int eal_args = 0;
	eal_args = rte_eal_init(argc, argv);
	assert(eal_args > 0);
	eal_args += 1;

	run_command(argc - eal_args, argv + eal_args, commands);

	return 0;
}
