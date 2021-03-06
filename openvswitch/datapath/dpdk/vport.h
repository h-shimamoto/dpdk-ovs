/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
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

#ifndef __VPORT_H_
#define __VPORT_H_

#include <stdint.h>
#include <rte_mbuf.h>

#include "kni.h"

#define MAX_VPORTS          48
#define MAX_PHYPORTS        16
#define MAX_CLIENTS         16
#define PKT_BURST_SIZE      32
#define CLIENT0             0
#define CLIENT1             1
#define KNI0                0x20
#define CLIENT_MASK         0x00
#define PORT_MASK           0x0F
#define KNI_MASK            0x1F
#define IS_CLIENT_PORT(action) ((action) > CLIENT_MASK && (action) <= PORT_MASK)
#define IS_PHY_PORT(action) ((action) > PORT_MASK && (action) <= KNI_MASK)
#define IS_KNI_PORT(action) ((action) > KNI_MASK  && (action) <= (KNI_MASK + MAX_KNI_PORTS))

struct port_info {
	uint8_t num_ports;
	uint8_t id[RTE_MAX_ETHPORTS];
};

struct port_info *ports;

void vport_init(void);
void vport_fini(void);
int send_to_client(uint8_t client, struct rte_mbuf *buf);
int send_to_port(uint8_t vportid, struct rte_mbuf *buf);
int send_to_kni(uint8_t vportid, struct rte_mbuf *buf);
uint16_t receive_from_port(uint8_t vportid, struct rte_mbuf **bufs);
uint16_t receive_from_kni(uint8_t vportid, struct rte_mbuf **bufs);
uint16_t receive_from_client(uint8_t client, struct rte_mbuf **bufs);

#endif /* __VPORT_H_ */


