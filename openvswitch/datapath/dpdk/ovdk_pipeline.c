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

/*
 * ovdk_pipeline implements an adapter for rte_pipeline.
 *
 * The ovdk_pipeline interface adapts ovdk_datapath to rte_pipeline.
 *
 * Each rte_pipeline is wrapped by an ovdk_pipeline. There should be one
 * rte_pipeline per core in order to process packets for that core.
 *
 * A user of ovdk_pipeline should call ovdk_pipeline_run() in order to process
 * packets received by that pipeline.
 */

#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/un.h>

#include <assert.h>

#include <rte_config.h>
#include <rte_string_fns.h>
#include <rte_table_hash.h>
#include <rte_table_stub.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_cycles.h>

#include "rte_port_ivshm.h"
#include "ovdk_pipeline.h"
#include "ovdk_hash.h"
#include "ovdk_flow.h"
#include "ovdk_vport.h"
#include "ovdk_mempools.h"
#include "ovdk_datapath_messages.h"
#include "ovdk_stats.h"

#define NO_FLAGS                        0
#define PKT_BURST_SIZE                  32u
#define EXCEPTION_PKT_BURST_SIZE        32u
#define VSWITCHD_RINGSIZE               2048
#define PIPELINE_NAME_PREFIX            "ovdk_pipeline_%02u"
#define OVDK_PIPELINE_FLUSH_INTERVAL    32
#define DPIF_SOCKNAME                   "\0dpif-dpdk"

#define RTE_LOGTYPE_APP                 RTE_LOGTYPE_USER1

/* offsets to metadata in mbufs as required by rte_pipeline */
#define OVDK_PIPELINE_SIG_OFFSET        0
#define OVDK_PIPELINE_KEY_OFFSET        32
#define OVDK_PIPELINE_PORT_OFFSET       64

/* Each flow table entry has a 'ovdk_pipeline_entry' associated with
 * it one-to-one */
struct ovdk_pipeline_entry {
	struct rte_pipeline_table_entry pf_entry;    /* each flow table entry
	                                              * must have an
	                                              * rte_pipeline_table_entry
	                                              * as it is required by the
	                                              * packet framework */
	uint8_t num_actions;                         /* number of actions
	                                              * in entry */
	struct ovdk_action actions[OVDK_MAX_ACTIONS];/* actions to carry out on
	                                              * pkts matching flow */
	struct ovdk_flow_stats stats;                /* stats for flow table
	                                              * entry */
};

/* Each lcore has a struct ovdk_pipeline */
struct ovdk_pipeline {
	struct rte_pipeline_params params;     /* 'params' used to configure
	                                        * rte_pipeline running on this
	                                        * core. */
	struct rte_pipeline *pf_pipeline;      /* handle of rte_pipeline
	                                        * running on this core */
	uint32_t table_id;                     /* id of main hash table running
	                                        * on this pipeline */
	uint32_t exception_table_id;           /* id of exception hash table
	                                          running on this pipeline */
	int dpif_socket;
	int iteration_count;                   /* Used in calculating when to
                                                  flush the pipeline */
	bool wakeup_required;                  /* Signal to wakeup vswitchd */
} __rte_cache_aligned;

struct ovdk_pipeline ovdk_pipeline[RTE_MAX_LCORE];

static struct rte_ring *create_ring(const char *name);

/* Handle actions */
static int ovdk_pipeline_exception_actions_execute(struct rte_mbuf **pkts,
        uint64_t *pkts_mask, struct rte_pipeline_table_entry *entry,
        void *arg);
static int ovdk_pipeline_actions_execute(struct rte_mbuf **pkts,
        uint64_t *pkts_mask, struct rte_pipeline_table_entry **entries,
        void *arg);
static inline int actions_execute(struct rte_mbuf *mbuf,
                                  struct ovdk_action *actions,
                                  uint8_t num_actions);
static inline int exception_actions_execute(struct rte_mbuf *mbuf,
                                            struct ovdk_action *actions,
                                            uint8_t num_actions);
static inline int action_execute(const struct ovdk_action *action,
                                 struct rte_mbuf *mbuf);
static inline void update_flow_stats(struct rte_mbuf *pkt,
                                     struct ovdk_flow_stats *stats,
                                     uint64_t tsc);
static void send_signal_to_dpif(void);
static inline int clone_packet(struct rte_mbuf **dest, struct rte_mbuf **src);
static inline int prepend_upcall(struct rte_mbuf *mbuf, uint8_t cmd);

/* Handle exception packets */
static int ovdk_pipeline_send_exception_to_vswitchd(struct rte_mbuf **pkts,
        uint64_t *pkts_mask, struct rte_pipeline_table_entry *entry,
        void *arg);

static void send_signal_to_dpif(void)
{
	static struct sockaddr_un addr;
	int n;
	unsigned lcore_id = rte_lcore_id();

	if (!addr.sun_family) {
		addr.sun_family = AF_UNIX;
		memcpy(addr.sun_path, DPIF_SOCKNAME, sizeof(DPIF_SOCKNAME));
	}

	/* don't care about error */
	sendto(ovdk_pipeline[lcore_id].dpif_socket, &n, sizeof(n), 0,
		(struct sockaddr *)&addr, sizeof(addr));
}

static struct rte_ring *
create_ring(const char *name)
{
	struct rte_ring *ring = NULL;
	char ring_name[OVDK_MAX_NAME_SIZE] = {0};
	unsigned lcore_id = rte_lcore_id();
	unsigned socket_id = rte_socket_id();

	snprintf(ring_name, sizeof(ring_name), name, lcore_id);
	ring = rte_ring_create(ring_name, VSWITCHD_RINGSIZE, socket_id,
	                       NO_FLAGS);
	if (ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create %s", ring_name);

	RTE_LOG(INFO, APP, "Created %s\n", ring_name);

	return ring;
}

/*
 * Initialise an rte_pipeline
 *
 * This function will initialize an rte_pipeline for execution on the core
 * from which this function is executed.
 *
 * This function will create a struct rte_pipeline and store the parameters
 * used to create it and the handle.
 *
 * This function will also set up the exception ring, packet ring and tables
 * required for this pipeline.
 */
void
ovdk_pipeline_init(void)
{
	char pipeline_name[OVDK_MAX_NAME_SIZE] = {0};
	struct rte_table_hash_key32_lru_params table_hash_params = {0};
	struct rte_pipeline_table_params table_params = {0};
	struct rte_pipeline_table_params exception_table_params = {0};
	struct rte_port_ivshm_reader_params port_packet_params =
	        {{0}, {0}, {0}, {0}};
	struct rte_pipeline_port_in_params port_in_packet_params = {0};
	struct rte_port_ivshm_writer_params port_exception_params =
	        {{0}, 0};
	struct rte_pipeline_port_out_params port_out_exception_params = {0};
	struct rte_pipeline_table_entry default_entry = {0};
	unsigned lcore_id = rte_lcore_id();
	unsigned socket_id = rte_socket_id();
	uint32_t exception_port_id = 0;
	uint32_t packet_port_id = 0;
	struct rte_pipeline_table_entry *default_entry_handle = NULL;
	int opt = 1;

	/*
	 * Store parameters used to configure the pipeline in 'ovdk_pipeline'.
	 */
	snprintf(pipeline_name, sizeof(pipeline_name),
	             PIPELINE_NAME_PREFIX, lcore_id);
	ovdk_pipeline[lcore_id].params.name = pipeline_name;
	ovdk_pipeline[lcore_id].params.socket_id = socket_id;
	ovdk_pipeline[lcore_id].params.offset_port_id = OVDK_PIPELINE_PORT_OFFSET;

	/* Create a pipeline and store the handle in 'ovdk_pipeline' */
	ovdk_pipeline[lcore_id].pf_pipeline =
		rte_pipeline_create(&ovdk_pipeline[lcore_id].params);
	if (ovdk_pipeline[lcore_id].pf_pipeline == NULL)
		rte_exit(EXIT_FAILURE, "Unable to configure pipeline on lcore"
			"'%u'\n", lcore_id);

	create_ring(VSWITCHD_PACKET_RING_NAME);
	create_ring(VSWITCHD_EXCEPTION_RING_NAME);
	create_ring(VSWITCHD_PACKET_FREE_RING_NAME);
	create_ring(VSWITCHD_PACKET_ALLOC_RING_NAME);

	/*
	 * Setup hash table in the pipeline.
	 */
	table_hash_params = (struct rte_table_hash_key32_lru_params) {
		.n_entries = OVDK_FLOW_TABLE_SIZE,
		.signature_offset = OVDK_PIPELINE_SIG_OFFSET,
		.key_offset = OVDK_PIPELINE_KEY_OFFSET,
		.f_hash = test_hash,
		.seed = 0,
	};

	table_params = (struct rte_pipeline_table_params) {
		.ops = &rte_table_hash_key32_lru_ops,
		.arg_create = &table_hash_params,
		/* each time an entry is hit f_action_hit() is called */
		.f_action_hit = ovdk_pipeline_actions_execute,
		.f_action_miss = ovdk_pipeline_send_exception_to_vswitchd,
		.arg_ah = NULL,
		/* each entry contains an ovdk_pipeline_entry struct */
		.action_data_size = sizeof(struct ovdk_pipeline_entry),
	};

	if (rte_pipeline_table_create(ovdk_pipeline[lcore_id].pf_pipeline,
	                              &table_params,
	                              &ovdk_pipeline[lcore_id].table_id))
		rte_exit(EXIT_FAILURE, "Unable to configure the hash table"
		         " (LRU) [pipeline '%s']\n",
		         ovdk_pipeline[lcore_id].params.name);

	/*
	 * Setup exception table in the pipeline.
	 */

	exception_table_params = (struct rte_pipeline_table_params) {
		.ops = &rte_table_stub_ops,
		.arg_create = NULL,
		/* Don't call anything on hit - This is a stub table*/
		.f_action_hit = NULL,
		/* Call special exception action handler on miss */
		.f_action_miss = ovdk_pipeline_exception_actions_execute,
		.arg_ah = NULL,
		.action_data_size = 0,
	};

	if (rte_pipeline_table_create(ovdk_pipeline[lcore_id].pf_pipeline,
	                              &exception_table_params,
	                              &ovdk_pipeline[lcore_id].exception_table_id))
		rte_exit(EXIT_FAILURE, "Unable to configure the exception table"
		         " [pipeline '%s']\n",
		         ovdk_pipeline[lcore_id].params.name);

	/*
	 * When we miss on the stub table, the default is to drop.
	 */
	default_entry = (struct rte_pipeline_table_entry) {
	                .action = RTE_PIPELINE_ACTION_DROP,
	};

	rte_pipeline_table_default_entry_add(
	                ovdk_pipeline[lcore_id].pf_pipeline,
	                ovdk_pipeline[lcore_id].exception_table_id,
	                &default_entry,
	                &default_entry_handle);
	/*
	 * Create a port to handle packet messages from vswtichd. Packet
	 * messages are messages sent by the vswitchd that contain packet data
	 * and a list of actions to execute on that packet.
	 */

	snprintf(port_packet_params.rx_ring_name,
	             sizeof(port_packet_params.rx_ring_name),
	             VSWITCHD_PACKET_RING_NAME,
	             lcore_id);

	snprintf(port_packet_params.free_ring_name,
	             sizeof(port_packet_params.free_ring_name),
	             VSWITCHD_PACKET_FREE_RING_NAME,
	             lcore_id);

	snprintf(port_packet_params.alloc_ring_name,
	             sizeof(port_packet_params.alloc_ring_name),
	             VSWITCHD_PACKET_ALLOC_RING_NAME,
	             lcore_id);

	snprintf(port_packet_params.mp,
	             sizeof(port_packet_params.mp),
	             PKTMBUF_POOL_NAME);

	port_in_packet_params = (struct rte_pipeline_port_in_params) {
		.ops = &rte_port_ivshm_reader_ops,
		.arg_create = (void *) &port_packet_params,
		.f_action = NULL,
		.arg_ah = NULL,
		.burst_size = PKT_BURST_SIZE,
	};

	if (rte_pipeline_port_in_create(ovdk_pipeline[lcore_id].pf_pipeline,
	                                &port_in_packet_params,
	                                &packet_port_id))
		rte_exit(EXIT_FAILURE, "Unable to configure packet ring"
		         " [pipeline '%s']\n",
		         ovdk_pipeline[lcore_id].params.name);

	/*
	 * As packet messages are handled by the pipeline we must attach them
	 * to the exception table in the pipeline
	 */
	if (rte_pipeline_port_in_connect_to_table(
	                ovdk_pipeline[lcore_id].pf_pipeline,
	                packet_port_id,
	                ovdk_pipeline[lcore_id].exception_table_id))
		rte_exit(EXIT_FAILURE, "Unable to connect input port '%u' to"
		         " exception table '%u' [pipeline '%s']\n",
		         packet_port_id,
		         ovdk_pipeline[lcore_id].exception_table_id,
		         ovdk_pipeline[lcore_id].params.name);

	if (rte_pipeline_port_in_enable(ovdk_pipeline[lcore_id].pf_pipeline,
	                                packet_port_id)) {
		rte_exit(EXIT_FAILURE, "Unable to enable input port '%u'"
		         " [pipeline '%s']\n", packet_port_id,
		         ovdk_pipeline[lcore_id].params.name);
	}

	/*
	 * Create a port to handle exception packets from the dataplane.
	 * Exception packets are packets that miss on the flow table and
	 * need further classification by the vswitchd. Typically they are
	 * the first packets of a flow received by the dataplane. At this
	 * point, no entry exists for them in the dataplane flow table.
	 * However after the vswtichd has classified the packet, a flow will
	 * be installed in the dataplane flow table.
	 */

	snprintf(port_exception_params.tx_ring_name,
	             sizeof(port_exception_params.tx_ring_name),
	             VSWITCHD_EXCEPTION_RING_NAME,
	             lcore_id);

	port_exception_params.tx_burst_sz = EXCEPTION_PKT_BURST_SIZE;

	port_out_exception_params = (struct rte_pipeline_port_out_params) {
		.ops = &rte_port_ivshm_writer_ops,
		.arg_create = (void *) &port_exception_params,
		.f_action = NULL,
		.arg_ah = NULL,
	};

	if (rte_pipeline_port_out_create(ovdk_pipeline[lcore_id].pf_pipeline,
	                &port_out_exception_params, &exception_port_id))
		rte_exit(EXIT_FAILURE, "Unable to configure exception port"
		         " [pipeline '%s']\n",
		         ovdk_pipeline[lcore_id].params.name);

	/*
	 * When we miss on the hash_table, the default is to send an
	 * exception packet to the vswitchd.
	 */
	default_entry = (struct rte_pipeline_table_entry) {
		.action = RTE_PIPELINE_ACTION_PORT,
		.port_id = exception_port_id,
	};

	rte_pipeline_table_default_entry_add(
	                ovdk_pipeline[lcore_id].pf_pipeline,
	                ovdk_pipeline[lcore_id].table_id, &default_entry,
	                &default_entry_handle);

	/* Check pipeline consistency */
	if (rte_pipeline_check(ovdk_pipeline[lcore_id].pf_pipeline) < 0)
		rte_exit(EXIT_FAILURE, "Pipeline consistency check failed"
		         " [pipeline '%s']\n",
		         ovdk_pipeline[lcore_id].params.name);

	/*
	 * Initialize socket used to wakeup the vswitchd
	 */
	ovdk_pipeline[lcore_id].dpif_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (ovdk_pipeline[lcore_id].dpif_socket < 0)
		rte_exit(EXIT_FAILURE, "Cannot create socket");

	if (ioctl(ovdk_pipeline[lcore_id].dpif_socket, FIONBIO, &opt) < 0)
		rte_exit(EXIT_FAILURE, "Cannot make socket non-blocking");
	return;
}

/*
 * Run ovdk_pipeline.
 *
 * This function executes one iteration of rte_pipeline_run() for the pipeline
 * associated with this core on which it is executed.
 */
int
ovdk_pipeline_run(void)
{
	unsigned lcore_id = 0;
	struct rte_pipeline *pf_pipeline = NULL;

	lcore_id = rte_lcore_id();
	pf_pipeline = ovdk_pipeline[lcore_id].pf_pipeline;

	rte_pipeline_run(pf_pipeline);
	if(ovdk_pipeline[lcore_id].iteration_count++ >=
	                                        OVDK_PIPELINE_FLUSH_INTERVAL) {
		rte_pipeline_flush(pf_pipeline);
		ovdk_pipeline[lcore_id].iteration_count = 0;
		if (ovdk_pipeline[lcore_id].wakeup_required) {
			send_signal_to_dpif();
			ovdk_pipeline[lcore_id].wakeup_required = false;
		}

	}

	return 0;
}

/*
 * Add the vport, referenced by 'vportid', to the rte_pipeline running
 * on this core as an in port.
 */
int
ovdk_pipeline_port_in_add(uint32_t vportid, char *vport_name)
{
	struct rte_pipeline_port_in_params *port_in_params = NULL;
	uint32_t port_in_id = 0;
	unsigned lcore_id = 0;
	enum ovdk_vport_state vport_state = OVDK_VPORT_STATE_FAILED;
	enum ovdk_vport_state prev_state = OVDK_VPORT_STATE_NEVER_USED;
	int ret = 0;

	lcore_id = rte_lcore_id();

	/* Get the state of the vport */
	if ((ret = ovdk_vport_get_state(vportid, &prev_state))) {
		RTE_LOG(WARNING, APP, "Unable to get vport state for in-port"
		        " '%u' [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	/*
	 * Set the state of the vport to failed initially. This is
	 * to guard against the port being left in an inconsistent
	 * state if an error occurs at any stage while creating
	 * required port in elements or if we fail to set/get a
	 * port state. If this initial state set call fails the
	 * port state will be inaccessible so we return an error.
	 */
	if ((ret = ovdk_vport_set_state(vportid, &vport_state))) {
		RTE_LOG(WARNING, APP, "Unable to set vport state for in-port"
		        " '%u' [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	/*
	 * Check if the vport_state is never used or failed. If so then create
	 * the required port_in params, create port_in, set the vport_id and
	 * connect the port to the table. This is only required if a port has
	 * never been used before or is in the failed state. Else, if a port is
	 * in the disabled state we need to get the existing port_in_id for that 
	 * in port.
	 */
	if ((prev_state == OVDK_VPORT_STATE_NEVER_USED) ||
	    (prev_state == OVDK_VPORT_STATE_FAILED)) {
		if ((ret = ovdk_vport_get_in_params(vportid, &port_in_params))) {
			RTE_LOG(WARNING, APP, "Unable to get params for in-port"
			        " '%u' [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		if ((ret = rte_pipeline_port_in_create(
		        ovdk_pipeline[lcore_id].pf_pipeline,
		        port_in_params, &port_in_id))) {
			RTE_LOG(WARNING, APP, "Unable to add in-port '%u'"
			        " [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		if ((ret = ovdk_vport_set_in_portid(vportid, port_in_id))) {
			RTE_LOG(WARNING, APP, "Unable to set vportid for"
			        " in-port '%u' [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		if ((ret = rte_pipeline_port_in_connect_to_table(
		        ovdk_pipeline[lcore_id].pf_pipeline, port_in_id,
		        ovdk_pipeline[lcore_id].table_id))) {
			RTE_LOG(WARNING, APP, "Unable to connect in-port '%u'"
			        " [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}
	}
	else {
		if ((ret = ovdk_vport_get_in_portid(vportid, &port_in_id))) {
			RTE_LOG(WARNING, APP, "Unable to get vportid for"
				" in-port '%u' [pipeline '%s']\n", vportid,
				ovdk_pipeline[lcore_id].params.name);
			return ret;
		}
	}

	/*
	 * Set the port name regardless of whether the previous port state
	 * is first use or used before.
	 */
	if ((ret = ovdk_vport_set_port_name(vportid, vport_name))) {
		RTE_LOG(WARNING, APP, "Unable to set vport name for in-port '%u'"
		        " [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	/*
	 * Enable the port regardless of whether the previous port state is
	 * first use or used before.
	 */
	if ((ret = rte_pipeline_port_in_enable(
	                ovdk_pipeline[lcore_id].pf_pipeline, port_in_id))) {
		RTE_LOG(WARNING, APP, "Unable to enable in-port '%u'"
		        " [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	/*
	 * At this point the port has been created/enabled without issue.
	 * We check the state stored previously for the vport info to discern
	 * whether the port has been used before. From this we can tell what
	 * in_use state to set (first use or used before). This is required
	 * so that we know whether corresponding out-ports need to be created
	 * or are already present in the pipelines.
	 */
	switch (prev_state) {
	case OVDK_VPORT_STATE_FAILED:
	case OVDK_VPORT_STATE_NEVER_USED:
		vport_state = OVDK_VPORT_STATE_IN_USE_FIRST_USE;
		break;
	case OVDK_VPORT_STATE_DISABLED:
		vport_state = OVDK_VPORT_STATE_IN_USE_USED_BEFORE;
		break;
	case OVDK_VPORT_STATE_IN_USE_FIRST_USE:
	case OVDK_VPORT_STATE_IN_USE_USED_BEFORE:
	default:
		vport_state = OVDK_VPORT_STATE_FAILED;
	}

	if ((ret = ovdk_vport_set_state(vportid, &vport_state))) {
		RTE_LOG(WARNING, APP, "Unable to set vport state for in-port '%u'"
		        " [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	return 0;
}

/*
 * Add the vport, referenced by 'vportid', to the rte_pipeline running on this
 * core as an out port.
 */
int
ovdk_pipeline_port_out_add(uint32_t vportid)
{
	struct rte_pipeline_port_out_params *port_out_params = NULL;
	uint32_t port_out_id = 0;
	unsigned lcore_id = 0;
	enum ovdk_vport_state vport_state = OVDK_VPORT_STATE_FAILED;
	enum ovdk_vport_state prev_state = OVDK_VPORT_STATE_NEVER_USED;
	int ret = 0;

	lcore_id = rte_lcore_id();

	/* Get the state of the vport */
	if ((ret = ovdk_vport_get_state(vportid, &prev_state))) {
		RTE_LOG(WARNING, APP, "Unable to get vport state for out-port '%u'"
		        " [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	/*
	 * At this point the bridge port status is OVDK_VPORT_STATE_NEVER_USED
	 * as only OUT ports are added.
	 */
	if ((OVDK_VPORT_TYPE_BRIDGE <= vportid && vportid < OVDK_VPORT_TYPE_VETH) &&
	        (prev_state == OVDK_VPORT_STATE_NEVER_USED))
		prev_state = OVDK_VPORT_STATE_IN_USE_FIRST_USE;

	/*
	 * Check if the prev_state is in_use_first_use. If so then
	 * create required port_out params, create port_out and set the vport
	 * port out id. This is only required if this is the first time a port
	 * has been used. Otherwise the out ports have been created previously.
	 */
	if (prev_state == OVDK_VPORT_STATE_IN_USE_FIRST_USE) {

		/*
		 * Set the state of the vport to failed initially. This is
		 * to guard against the port being left in an inconsistent
		 * state if an error occurs at any stage while creating
		 * required port out elements or if we fail to set/get a
		 * port state. If this initial state set call fails the
		 * port state will be inaccessible so we return an error.
		 */
		if ((ret = ovdk_vport_set_state(vportid, &vport_state))) {
			RTE_LOG(WARNING, APP, "Unable to set vport state for"
			        " out-port '%u' [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		if ((ret = ovdk_vport_get_out_params(vportid, &port_out_params))) {
			RTE_LOG(WARNING, APP, "Unable to get params for"
			        " out-port '%u' [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		if ((ret = rte_pipeline_port_out_create(
		        ovdk_pipeline[lcore_id].pf_pipeline,
		        port_out_params, &port_out_id))) {
			RTE_LOG(WARNING, APP, "Unable to add out-port '%u'"
			        " [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		if ((ret = ovdk_vport_set_out_portid(vportid, port_out_id))) {
			RTE_LOG(WARNING, APP, "Unable to set vportid for"
			        " out-port '%u' [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		/*
		 * We can asssume that all port out elements have been created
		 * without issue. We set the vports state back to its
		 * previous state.
		 */
		if ((ret = ovdk_vport_set_state(vportid, &prev_state))) {
			RTE_LOG(WARNING, APP, "Unable to set vport state for"
			        " out-port '%u' [pipeline '%s']\n", vportid,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}
	}

	return 0;
}

/*
 * Add an flow table entry to the rte_pipeline running on this core. This entry
 * is described by a flow key 'key' and a list 'num_actions' actions.
 */
int
ovdk_pipeline_flow_add(struct ovdk_flow_key *key, struct ovdk_action *actions,
                       uint8_t num_actions, uint64_t *flow_handle)
{
	unsigned port_id = 0;
	uint8_t action_count = 0;
	uint32_t port_in_id = 0;
	struct ovdk_pipeline_entry ovdk_pipeline_entry = {{0}, 0, {{0}, {0}},{0}};
	struct ovdk_flow_key pipeline_key = {0};
	struct rte_pipeline_table_entry *entry_handle = NULL;
	int is_key_found = 0;
	unsigned lcore_id = 0;
	int ret = 0;

	lcore_id = rte_lcore_id();

	ovdk_pipeline_entry.num_actions = num_actions;

	for (action_count = 0; action_count < num_actions; action_count++) {
		if (actions[action_count].type == OVDK_ACTION_OUTPUT) {
			if ((ret = ovdk_vport_get_out_portid(
			        actions[action_count].data.output.port,
			        &port_id))) {
				RTE_LOG(WARNING, APP, "Unable to get vportid"
				        " for out-port '%u' [pipeline '%s']\n",
				        port_id,
				        ovdk_pipeline[lcore_id].params.name);
				return ret;
			}

			/*
			 * First output action gets handled by RTE_PIPELINE_ACTION_PORT but the
			 * other actions get handled by the tables f_action() callback.
			 *
			 * Therefore if 'actions' contains multiple output actions then the
			 * following would happen when executing all actions on a packet:
			 *
			 * - The hash table f_action() handler executes all actions.
			 * - When executing the first output action, the
			 *   RTE_PIPELINE_ACTION_PORT specified below will cause the packet
			 *   to be outputted to the output port.
			 * - Subsequent output actions will cause the mbuf to be cloned and
			 *   outputted using another mechanism.
			 *
			 *   This means that anytime there are multiple output actions, the
			 *   mbuf gets cloned so that any modifications that are made will not
			 *   affect the mbuf from the previous output action.
			 */
			ovdk_pipeline_entry.pf_entry.action = RTE_PIPELINE_ACTION_PORT;
			ovdk_pipeline_entry.pf_entry.port_id = port_id;


			break;
		} else if (actions[action_count].type == OVDK_ACTION_DROP) {
			/*
			 * Only support case of drop as
			 * a single action
			 */
			assert(num_actions == 1);
			ovdk_pipeline_entry.pf_entry.action = RTE_PIPELINE_ACTION_DROP;
			break;
		} else if (actions[action_count].type == OVDK_ACTION_VSWITCHD) {
			/*
			 * Only support case of send to userspace as
			 * a single action
			 */
			assert(num_actions == 1);
			/* send to exception port */
			port_id = 0;
			ovdk_pipeline_entry.pf_entry.action = RTE_PIPELINE_ACTION_PORT;
			ovdk_pipeline_entry.pf_entry.port_id = port_id;
			break;
		}
	}

	rte_memcpy(&ovdk_pipeline_entry.actions, actions,
	           sizeof(struct ovdk_action) * OVDK_MAX_ACTIONS);

	/* convert the vportid in the key to an in port id for this core */
	pipeline_key = *key;
	ovdk_vport_get_in_portid(pipeline_key.in_port, &port_in_id);
	pipeline_key.in_port = port_in_id;

	if ((ret = rte_pipeline_table_entry_add(
	                ovdk_pipeline[lcore_id].pf_pipeline,
	                ovdk_pipeline[lcore_id].table_id,
	                &pipeline_key,
	                (struct rte_pipeline_table_entry *)&ovdk_pipeline_entry,
			&is_key_found,
			&entry_handle))) {
		RTE_LOG(WARNING, APP, "Unable to add flow [pipeline '%s']"
		        "\n", ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	/*
	 * Store the flow entry handle so that it may be returned as part of the
	 * reply to the 'flow_new' command received from the daemon.
	 */
	*flow_handle = (uint64_t)entry_handle;

	return ret;
}

/*
 * Delete the flow table entry referenced by 'key' from the rte_pipeline
 * running on this core
 */
int
ovdk_pipeline_flow_del(struct ovdk_flow_key *key,
                       int *key_found,
                       struct ovdk_flow_stats *stats)
{
	unsigned lcore_id = 0;
	int is_key_found = 0;
	int ret = 0;
	struct ovdk_flow_key pipeline_key = {0};
	uint32_t port_in_id = 0;
	struct ovdk_pipeline_entry flow_entry = {{0}, 0, {{0}, {0}},{0}};

	lcore_id = rte_lcore_id();

	/* convert the vportid in the key to an in port id for this core */
	pipeline_key = *key;
	ovdk_vport_get_in_portid(key->in_port, &port_in_id);
	pipeline_key.in_port = port_in_id;

	if ((ret = rte_pipeline_table_entry_delete(
	                ovdk_pipeline[lcore_id].pf_pipeline,
	                ovdk_pipeline[lcore_id].table_id, &pipeline_key,
			&is_key_found, (struct rte_pipeline_table_entry *) &flow_entry))) {
		RTE_LOG(WARNING, APP, "Unable to delete flow [pipeline "
		        "'%s']\n",
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}
	if (key_found != NULL)
	    *key_found = is_key_found;

	if (stats) {
		*stats = flow_entry.stats;
	}

	return ret;
}

/*
 * Retrieve flow stats from the entry indexed by 'flow_handle'.
 */
int
ovdk_pipeline_flow_get_stats(struct ovdk_flow_stats *stats, uint64_t flow_handle)
{
	struct ovdk_pipeline_entry *flow_entry = NULL;

	if ((stats == NULL) || (flow_handle ==0))
		return EINVAL;

	flow_entry = (struct ovdk_pipeline_entry *)flow_handle;

	*stats = flow_entry->stats;

	/* Convert Timestamp */
	if(stats->used) {
		stats->used = ovs_flow_used_time(rte_rdtsc(), stats->used);
	}
	else {
		stats->used = 0;
	}
	return 0;
}

/*
 * Amend flow stats from the entry indexed by 'flow_handle'.
 */
int
ovdk_pipeline_flow_set_stats(struct ovdk_flow_stats *stats, uint64_t flow_handle)
{

	struct ovdk_pipeline_entry *flow_entry = NULL;

	if ((stats == NULL) || (flow_handle ==0))
		return EINVAL;

	flow_entry = (struct ovdk_pipeline_entry *)flow_handle;

	flow_entry->stats = *stats;
	return 0;
}

/*
 * Retrieve flow actions from the entry indexed by 'flow_handle'.
 */
int
ovdk_pipeline_flow_get_actions(struct ovdk_action *actions,
                               uint8_t *num_actions,
                               uint64_t flow_handle)
{
	struct ovdk_pipeline_entry *flow_entry = NULL;

	if ((actions == NULL) || (num_actions == NULL) ||(flow_handle == 0))
		return EINVAL;

	flow_entry = (struct ovdk_pipeline_entry *)flow_handle;

	*num_actions = flow_entry->num_actions;
	rte_memcpy(actions, flow_entry->actions,
	                         sizeof(struct ovdk_action) * OVDK_MAX_ACTIONS);
	return 0;
}


/*
 * Delete the vport, referenced by 'vportid', from the rte_pipeline running
 * on this core as an in port.
 */
int
ovdk_pipeline_port_in_del(uint32_t vportid)
{
	unsigned port_id = 0;
	unsigned lcore_id = 0;
	enum ovdk_vport_state vport_state = OVDK_VPORT_STATE_FAILED;
	int ret = 0;

	lcore_id = rte_lcore_id();

	/*
	 * Set the state of the vport to failed initially. This is
	 * to guard against the port being left in an inconsistent
	 * state if an error occurs at any stage while disabling
	 * the port or setting the state.
	 */
	if ((ret = ovdk_vport_set_state(vportid, &vport_state))) {
		RTE_LOG(WARNING, APP, "Unable to set vport state for in-port"
		        " '%u' [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	if ((ret = ovdk_vport_get_in_portid(vportid, &port_id))) {
		RTE_LOG(WARNING, APP, "Unable to get vportid for in-port '%u'"
		        " [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	if ((ret = rte_pipeline_port_in_disable(
	                ovdk_pipeline[lcore_id].pf_pipeline,
	                port_id))) {
		RTE_LOG(WARNING, APP, "Unable to get disable in-port '%u'"
		        " [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	/* vport is now disabled, set vport state to disabled */
	vport_state = OVDK_VPORT_STATE_DISABLED;
	if ((ret = ovdk_vport_set_state(vportid, &vport_state))) {
		RTE_LOG(WARNING, APP, "Unable to set vport state for in-port"
		        " '%u' [pipeline '%s']\n", vportid,
		        ovdk_pipeline[lcore_id].params.name);
		return ret;
	}

	return ret;
}

/*
 * Delete the vport referenced by 'vportid', from the rte_pipeline running
 * on this core as an out port.
 */
int
ovdk_pipeline_port_out_del(uint32_t vportid __rte_unused)
{
	int ret = 0;

	/*
	 * With the current packet framework, there is nothing to do
	 * here as we cannot delete an out port from the pipeline.
	 * We do not modify the vport_info entry because the
	 * port_out details will be needed if the port_in is
	 * re-enabled at a later stage. The function is kept
	 * for completeness only.
	 */
	return ret;
}

/*
 * This function is called by a pipeline every time rte_pipeline_run() is
 * executed on the exception table.
 *
 * 'pkts' is an array of up to 64 mbufs. 'pkts_mask' is a 64-bit unsigned
 * integer of which each bit represents one of the 64 mbufs in 'pkts'. If the
 * bit is set, it is expected that this function does two things to the
 * packet:
 *
 * 1. Update the stats for vswitchd rx.
 * 2. Execute the actions passed down from the vswitchd on this mbuf.
 */
static int
ovdk_pipeline_exception_actions_execute(struct rte_mbuf **pkts,
                              uint64_t *pkts_mask,
                              __attribute__((unused))
                              struct rte_pipeline_table_entry *entry,
                              __attribute__((unused)) void *arg)
{
	uint32_t i = 0;
	uint8_t n = 0;
	uint8_t num_actions = 0;
	struct ovdk_action actions[OVDK_MAX_ACTIONS] = {{0}};
	struct ovdk_message *request = NULL;

	n = __builtin_popcountll(*pkts_mask);

	/*
	 * Since this is a stub table action, we can assume that
	 * set bits are contiguous
	 */
	for (i = 0; i < n ; i++) {
		/* Get message info from pkts[i] */
		request = rte_pktmbuf_mtod(pkts[i], struct ovdk_message *);

		/* Remove the reqest from the beginning of the data */
		rte_pktmbuf_adj(pkts[i],
		                sizeof(struct ovdk_message));

		if (!(request->type == OVDK_PACKET_CMD_FAMILY)) {
			RTE_LOG(ERR, APP, "Unexpected message type"
			                  " received in packet port\n");
			rte_pktmbuf_free(pkts[i]);
			ovdk_stats_vswitch_data_rx_drop_increment(1);
			continue;
		}

		/*
		 * Copy actions from packet to prevent any modification
		 * of packet from affecting the array of actions in the
		 * packet message.
		 */
		num_actions = request->packet_msg.num_actions;
		rte_memcpy(actions, request->packet_msg.actions,
		           sizeof(struct ovdk_action) * num_actions);

		/* Execute the actions in 'actions' on 'pkts[i]' */
		exception_actions_execute(pkts[i], actions, num_actions);

		ovdk_stats_vswitch_data_rx_increment(1);
	}

	return 0;
}

/*
 * This function executes 'num_actions' actions from 'actions' on the packet
 * referenced by 'mbuf' for the exception table.
 *
 * If actions contains multiple output actions then the mbuf must be cloned.
 *
 * Output actions will be handled by rte_pipeline_port_out_packet_insert()
 * after cloning the mbuf.
 *
 */
static inline int
exception_actions_execute(struct rte_mbuf *mbuf, struct ovdk_action *actions,
                  uint8_t num_actions)
{
	const struct ovdk_action *action = NULL;
	uint8_t action_count = 0;
	struct rte_mbuf *mb = mbuf;
	/*
	 * mb_orig will be freed by the stub table default action (DROP) but
	 * we use it here to keep track of changes in the packet as it executes
	 * actions
	 */
	struct rte_mbuf *mb_orig = mbuf;
	int error = 0;
	for (action_count = 0; action_count < num_actions; action_count++) {
		mb = mb_orig;
		action = &actions[action_count];

		/*
		 * The default action for a stub table is DROP. Therefore
		 * before we OUTPUT a packet, we need to clone. This means
		 * there is a clone for every packet sent from the vswitchd.
		 *
		 * TODO
		 * We can improve this by using the PORT_META action as the
		 * default action for the table.
		 */
		if (action->type == OVDK_ACTION_OUTPUT) {
			/*
			 * There could be more output actions, copy this mbuf
			 * before we send it and lose its data.
			 */
			error = clone_packet(&mb, &mb_orig);
			if (error)
				return -1;
		}
		action_execute(action, mb);
	}
	return 0;
}

/*
 * This function is called by a pipeline every time rte_pipeline_run() is
 * executed.
 *
 * 'pkts' is an array of up to 64 mbufs. 'pkts_mask' is a 64-bit unsigned
 * integer of which each bit represents one of the 64 mbufs in 'pkts'. If the
 * bit is set, it is expected that this function does two things to the
 * packet:
 *
 * 1. Update the flow table stats for the flow table entry that this mbuf
 *    matched on.
 * 2. Execute the actions from the flow table entry on this mbuf.
 *
 */
static int
ovdk_pipeline_actions_execute(struct rte_mbuf **pkts, uint64_t *pkts_mask,
                              struct rte_pipeline_table_entry **entries,
                              void *arg __attribute__((unused)))
{
	uint32_t i = 0;
	struct ovdk_pipeline_entry *ovdk_pipeline_entry = NULL;
	uint64_t tsc = 0;

	/* Get the timestamp for a burst of packets */
	tsc = rte_rdtsc();

	/* unwrap the pkts_mask to determine what bits are set */
	for (i = 0; *pkts_mask != 0 && i < 64 ; i++) {
		uint64_t pkt_mask;

		pkt_mask = 1lu << i;
		if ((pkt_mask & *pkts_mask) == 0)
			continue;

		/*
		 * every 'pkts[i]' has a corresponding 'entries[i]'.
		 * 'entries[i]' is a list of actions to execute on 'pkts[i]'.
		 * There is a 1:1 correspondance.
		 */
		ovdk_pipeline_entry = (struct ovdk_pipeline_entry *)entries[i];

		/* execute the actions in 'entry->actions' on 'pkts[i]' */
		actions_execute(pkts[i], ovdk_pipeline_entry->actions,
		                ovdk_pipeline_entry->num_actions);
		/* updated the flow table stats in 'entry->stats' */
		update_flow_stats(pkts[i], &ovdk_pipeline_entry->stats, tsc);
	}

	return 0;
}

/*
 * This function executes 'num_actions' actions from 'actions' on the packet
 * referenced by 'mbuf'
 *
 * If actions contains multiple output actions then the mbuf must be cloned.
 *
 * The first output action will get handled by the RTE_PIPELINE_ACTION_PORT
 * action in rte_pipeline. Subsequent output actions will be handled by
 * rte_pipeline_port_out_packet_insert() after cloning the mbuf.
 *
 */
static inline int
actions_execute(struct rte_mbuf *mbuf, struct ovdk_action *actions,
                  uint8_t num_actions)
{
	const struct ovdk_action *action = NULL;
	uint8_t action_count = 0;
	bool first_output = true;
	struct rte_mbuf *mb = mbuf;
	struct rte_mbuf *mb_next = mbuf;
	uint8_t actions_remaining = 0;
	int error = 0;

	for (action_count = 0; action_count < num_actions; action_count++) {
		mb = mb_next;
		action = &actions[action_count];
		actions_remaining = (num_actions - (action_count + 1));

		/*
		 * After we execute the OVDK_ACTION_OUTPUT on a packet
		 * we can consider the mbuf sent. This means we need
		 * to clone the mbuf prior to sending it if we need to
		 * use it again.
		 */
		if (action->type == OVDK_ACTION_OUTPUT) {
			if (actions_remaining) {
				/*
				 * There could be more output actions, copy this mbuf
				 * before we send it and lose its data.
				 */
				error = clone_packet(&mb_next, &mb);
				if (error)
					return -1;

			}
			/*
			 * The first output action is handled by
			 * RTE_PIPELINE_ACTION_PORT, so we dont have to do anything
			 * to the mbuf as it will get sent by rte_pipeline_run(). This
			 * means that we can consider the mbuf sent at this stage.
			 * Therefore this means we need to clone the mbuf if we need
			 * to do something else to it. e.g. send to another port.
			 */
			if (likely(first_output)) {
				first_output = false;
				continue;
			}
		} else if (unlikely((action->type != OVDK_ACTION_VSWITCHD &&
		                    action->type != OVDK_ACTION_DROP))) {
			/*
			 * It should never be the case where the last action
			 * is not to output
			 */
			assert(actions_remaining);
		}

		action_execute(action, mb);
	}

	return 0;
}

/*
 * Using rte_pktmbuf_alloc() we don't get a full copy
 * of mbuf, just an "indirect" copy that references to
 * the same data of mbuf. The full copy is done by
 * allocating a new mbuf and copy the data over
 * avoiding possible race conditions when applying
 * multiple actions on the packet.
 */
static inline int
clone_packet(struct rte_mbuf **dest, struct rte_mbuf **src)
{
	struct rte_mempool *mp = NULL;
	mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);

	*dest = rte_pktmbuf_alloc((struct rte_mempool *)mp);
	if (*dest) {
		rte_memcpy((*dest)->pkt.data, (*src)->pkt.data,
			   rte_pktmbuf_data_len(*src));
		rte_pktmbuf_data_len(*dest) = rte_pktmbuf_data_len(*src);
		rte_pktmbuf_pkt_len(*dest) = rte_pktmbuf_pkt_len(*src);
		(*dest)->pkt.nb_segs = 1;
	} else {
		RTE_LOG(ERR, APP, "Failed to clone pktmbuf\n");
		return -1;
	}

	return 0;
}

/*
 * Do 'action' of action_type 'type' on 'mbuf'.
 */
static inline int
action_execute(const struct ovdk_action *action, struct rte_mbuf *mbuf)
{
	uint32_t port_id = 0;
	unsigned lcore_id = 0;
	int ret = 0;

	lcore_id = rte_lcore_id();

	switch (action->type) {
	case OVDK_ACTION_OUTPUT:
		if ((ret = ovdk_vport_get_out_portid(action->data.output.port,
		                                     &port_id))) {
			RTE_LOG(WARNING, APP, "Unable to get disable in-port "
			        "'%u' [pipeline '%s']\n", port_id,
			        ovdk_pipeline[lcore_id].params.name);
			return ret;
		}

		/* this can only return 0, so don't bother checking return */
		rte_pipeline_port_out_packet_insert(
		            ovdk_pipeline[lcore_id].pf_pipeline,
		            port_id, mbuf);
		break;
	case OVDK_ACTION_POP_VLAN:
		ovdk_action_pop_vlan(mbuf);
		break;
	case OVDK_ACTION_PUSH_VLAN:
		ovdk_action_push_vlan(&action->data.vlan, mbuf);
		break;
	case OVDK_ACTION_SET_ETHERNET:
		ovdk_action_set_ethernet(&action->data.ethernet, mbuf);
		break;
	case OVDK_ACTION_SET_IPV4:
		ovdk_action_set_ipv4(&action->data.ipv4, mbuf);
		break;
	case OVDK_ACTION_SET_TCP:
		ovdk_action_set_tcp(&action->data.tcp, mbuf);
		break;
	case OVDK_ACTION_SET_UDP:
		ovdk_action_set_udp(&action->data.udp, mbuf);
		break;
	case OVDK_ACTION_DROP:
		/* Do nothing as the packet framework action will do the actual drop */
		break;
	case OVDK_ACTION_VSWITCHD:
		ret = prepend_upcall(mbuf, PACKET_CMD_ACTION);
		if (ret) {
			ovdk_stats_vswitch_data_tx_drop_increment(1);
		} else {
			ovdk_pipeline[lcore_id].wakeup_required = true;
			ovdk_stats_vswitch_data_tx_increment(1);
		}
		break;
	case OVDK_ACTION_MAX:
	case OVDK_ACTION_NULL:
	default:
		RTE_LOG(ERR, APP, "Action not currently implemented\n");
		break;
	}

	return ret;
}
/* Handle exception packets */
static int
ovdk_pipeline_send_exception_to_vswitchd(struct rte_mbuf **pkts,
        uint64_t *pkts_mask,
        struct rte_pipeline_table_entry *entry __attribute__((unused)),
        void *arg __attribute__((unused)))
{
	uint32_t i = 0;
	int error = 0;
	unsigned lcore_id = 0;

	lcore_id = rte_lcore_id();

	/* Unwrap the pkts_mask to determine what bits are set */
	for (i = 0; *pkts_mask != 0 && i < 64 ; i++) {
		uint64_t pkt_mask = 1lu << i;
		if ((pkt_mask & *pkts_mask) == 0)
			continue;

		error = prepend_upcall(pkts[i], PACKET_CMD_MISS);
		if (error) {
			uint64_t pkt_err_mask = 1lu << i;
			RTE_LOG(ERR, APP, "Failed to prepend upcall"
			                  " information, dropping packet\n");
			ovdk_stats_vswitch_data_tx_drop_increment(1);
			*pkts_mask ^= pkt_err_mask;
			rte_pktmbuf_free(pkts[i]);
			continue;
		}

		ovdk_stats_vswitch_data_tx_increment(1);
	}

	ovdk_pipeline[lcore_id].wakeup_required = true;
	return 0;
}

static inline void
update_flow_stats(struct rte_mbuf *pkt, struct ovdk_flow_stats *stats,
                  uint64_t tsc)
{
	struct ovdk_flow_key *key = NULL;

	/*
	 * no need to check parameters for NULL as this is a static function
	 * that is only called once in this file. As a result, we know that
	 * the parameters will not be NULL
	 */
	key = (struct ovdk_flow_key *)RTE_MBUF_METADATA_UINT8_PTR(pkt,
	                                          OVDK_PIPELINE_KEY_OFFSET);

	if (key->ether_type == ETHER_TYPE_IPv4 &&
		key->ip_proto == IPPROTO_TCP) {
		struct tcp_hdr *tcp_hdr = TCP_HDR_FROM_PKT(pkt);
		stats->tcp_flags |= tcp_hdr->tcp_flags & TCP_FLAG_MASK;
	}

	stats->used = tsc;
	stats->packet_count++;
	stats->byte_count += rte_pktmbuf_data_len(pkt);

	return;
}


/*
 * Prepend an upcall to the mbuf using the specified 'cmd'
 */
static inline int
prepend_upcall(struct rte_mbuf *mbuf, uint8_t cmd)
{
	struct ovdk_upcall upcall = {0};
	struct ovdk_flow_key *key_ptr = NULL;
	int error = 0;
	uint32_t vportid = 0;
	void *mbuf_ptr = NULL;

	key_ptr = (void*)RTE_MBUF_METADATA_UINT8_PTR(mbuf,
                                      OVDK_PIPELINE_KEY_OFFSET);
	upcall.key = *key_ptr;
	upcall.cmd = cmd;

	/*
	 * Convert in_port representation from pipeline in_port to
	 * vportid
	 */
	error = ovdk_vport_get_vportid(upcall.key.in_port, &vportid);
	if (error) {
		RTE_LOG(ERR, APP, "Failed to get vportid when prepending "
		        "upcall (key.in_port = %d)\n", upcall.key.in_port);
		return -1;
	}

	upcall.key.in_port = (uint8_t)vportid;

	/* Discard everything except the data */
	rte_pktmbuf_pkt_len(mbuf) = rte_pktmbuf_data_len(mbuf);
	mbuf_ptr = rte_pktmbuf_prepend(mbuf, sizeof(upcall));
	if(mbuf_ptr == NULL) {
		RTE_LOG(ERR, APP, "Failed to prepend upcall\n");
		return -1;
	}

	rte_memcpy(mbuf_ptr, &upcall, sizeof(upcall));

	return 0;
}
