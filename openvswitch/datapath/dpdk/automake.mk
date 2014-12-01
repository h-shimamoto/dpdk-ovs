# BSD License
#
# Copyright(c) 2014 Intel Corporation. All rights reserved.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

bin_PROGRAMS += datapath/dpdk/ovs-dpdk
datapath_dpdk_ovs_dpdk_SOURCES = datapath/dpdk/ovdk_main.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_stats.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_pipeline.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_mempools.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vport_phy.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vport_client.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vport_vhost.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vport_bridge.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vport_veth.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vport_memnic.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vport.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/rte_port_ivshm.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/rte_port_vhost.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/rte_port_veth.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/rte_port_memnic.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_datapath.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_hash.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_jobs.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_init.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_action.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_flow.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_virtio-net.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_vhost-net-cdev.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ofpbuf_helper.c
datapath_dpdk_ovs_dpdk_SOURCES += datapath/dpdk/ovdk_args.c

datapath_dpdk_ovs_dpdk_CFLAGS = $(AM_CFLAGS)
datapath_dpdk_ovs_dpdk_CFLAGS += -D_FILE_OFFSET_BITS=64

# Added due to a limitation introduced by DPDK 1.7's Method of initialising phy ports
# Need to make sure all code is linked, as construction now happens through a macro,
# causing the linker to think the code is not used.
datapath_dpdk_ovs_dpdk_LDFLAGS = -Wl,--whole-archive $(dpdk_libs) -Wl,--no-whole-archive -ldl -lrt -lm

datapath_dpdk_ovs_dpdk_LDADD = lib/libopenvswitch.la $(SSL_LIBS)
datapath_dpdk_ovs_dpdk_LDADD += -lfuse
