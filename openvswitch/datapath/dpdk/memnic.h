/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2013-2014 NEC All rights reserved.
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

#ifndef MEMNIC_H
#define MEMNIC_H

#define MEMNIC_MAGIC		0x43494e76
#define MEMNIC_VERSION		0x00000001
#define MEMNIC_VERSION_1	0x00000001

#define MEMNIC_NR_PACKET	(1024)
#define MEMNIC_AREA_SIZE	(16 * 1024 * 1024)

#define MEMNIC_MAX_PACKET_SIZE	(4096)

#define MEMNIC_MAX_FRAME_LEN	(1500 + 14 + 4) /* MTU + ether header + vlan */
#define MEMNIC_MAX_JUMBO_FRAME_LEN	(MEMNIC_MAX_PACKET_SIZE - 8)

struct memnic_header {
	uint32_t magic;
	uint32_t version;
	uint32_t valid;
	uint32_t reset;
	uint32_t features;	/* features this MEMNIC provides */
	uint32_t request;	/* requesting features from Guest */
	union {
		uint8_t mac_addr[6];
		uint8_t dummy[8];
	};
	/* for extra features */
	uint32_t framesz;
};
#define MEMNIC_FEAT_FRAME_SIZE	(0x00000001)
#define MEMNIC_FEAT_ALL		(MEMNIC_FEAT_FRAME_SIZE)

struct memnic_info {
	uint32_t flags;
	uint32_t free;
	/* statistics */
	uint64_t pkts;
	uint64_t bytes;
};

struct memnic_packet {
	union {
		struct {
			uint32_t status;
			uint32_t len;
			uint8_t  data[0];
		};
		uint8_t packet_pad[MEMNIC_MAX_PACKET_SIZE];
	};
};

#define MEMNIC_PKT_ST_FREE	(0)
#define MEMNIC_PKT_ST_USED	(1)
#define MEMNIC_PKT_ST_FILLED	(2)

struct memnic_data {
	union {
		struct memnic_info info;
		char info_pad[1024 * 1024];
	};
	struct memnic_packet packets[MEMNIC_NR_PACKET];
};

/*
 * Shared memory area mapping
 * Because IVSHMEM size must be power of 2MB, use 16MB.
 * +------------------+ - - - - - - -
 * | Header Area  1MB | 8MB
 * +------------------+
 * | Up to VM     7MB | for uplink
 * +------------------+ - - - - - - -
 * | Reserved     1MB | 8MB
 * +------------------+
 * | Down to host 7MB | for downlink
 * +------------------+ - - - - - - -
 *
 * Reserved area is just for keeping the layout symmetric.
 */
struct memnic_area {
	union {
		struct memnic_header hdr;
		char hdr_pad[1024 * 1024];
	};
	union {
		struct memnic_data up;
		char up_pad[7 * 1024 * 1024];
	};
	char reserved[1024 * 1024];
	union {
		struct memnic_data down;
		char down_pad[7 * 1024 * 1024];
	};
};

#endif /* MEMNIC_H */
