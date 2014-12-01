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

#ifndef __OVDK_CONFIG_H_
#define __OVDK_CONFIG_H_

/* Compile time configuration */

/* The number of flow table entries in each core's table */
#define OVDK_FLOW_TABLE_SIZE     65536
/* TODO: same value as VPORTS increase if required */
#define OVDK_MAX_ACTIONS         48

/* Default maximum packet size */
#define OVDK_DEFAULT_MAX_FRAME_SIZE 1518

/*
 * This is the maximum number of ports of each type that can be used in the
 * dataplane. It is an arbitrary limit that can be changed at compile time.
 * The tradeoff here is the amount of memory needed for each port.
 */
#define OVDK_MAX_CLIENTS         32
#define OVDK_MAX_PHYPORTS        32
#define OVDK_MAX_VHOSTS          32
#define OVDK_MAX_KNIS            32
#define OVDK_MAX_BRIDGES         32
#define OVDK_MAX_VETHS           4
#define OVDK_MAX_MEMNICS         32
#define OVDK_MAX_VPORTS          ((OVDK_MAX_CLIENTS)  + \
                                 (OVDK_MAX_PHYPORTS) + \
                                 (OVDK_MAX_VHOSTS) + \
                                 (OVDK_MAX_KNIS) + \
                                 (OVDK_MAX_BRIDGES) + \
                                 (OVDK_MAX_VETHS) + \
                                 (OVDK_MAX_MEMNICS))
#endif /* __OVDK_CONFIG_H_ */
