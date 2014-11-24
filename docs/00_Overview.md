[Intel® DPDK vSwitch][dpdk-ovs-github] is a fork of the open source Open vSwitch multilayer virtual switch found at [openvswitch.org][ovs].

Intel® and Wind River teams re-created the kernel forwarding module (data plane) by building the switching logic on top of the Intel® DPDK library to significantly boost packet switching throughput. The forwarding engine incorporates Intel® DPDK Huge Page Tables. The Open vSwitch control daemon is modified to connect to Intel® DPDK Huge Page Tables. The forwarding module runs in Linux user space with BSD license rights. Intel® DPDK vSwitch implements a subset of the switching functionality of Open vSwitch.

______

## Introduction

Intel® DPDK vSwitch is a proofpoint Virtual Switch implemented using Intel® DPDK acceleration and Open vSwitch. The Virtual Switch is a key technology in realizing Network Function Virtualization. The aim of this proofpoint is to significantly improve the performance of Open vSwitch while maintaining its core functionality.

[QEMU][qemu] is a generic and open source machine emulator and virtualizer. The QEMU source code has been modified and provided here to enable efficient inter-VM communications by interfacing with an accelerated Open vSwitch and improving small-packet performance.

[Open vSwitch][ovs] is a production quality, multilayer virtual switch licensed under the open source Apache 2.0 license. The Open vSwitch source code has been modified and provided here to enable fast packet switching on Intel® Architecture and improve small-packet performance.

![Architecture Overview](ovdk_architecture_overview.png?raw=true "Architecture Overview")

____

## Features

### Inter Virtual Machine Communication Methods

Intel® DPDK vSwitch currently provides two methods of communicating between guest and host.

#### IVSHMEM

**Suggested use case:** Virtual Appliance running Linux with an Intel® DPDK based application.

* Zero copy between guest and switch.
* Option when applications are trusted and highest small packet throughput required.
* Option when applications do not need the Linux network stack.
* Opportunity to add VM to VM security through additional buffer allocation (via `memcpy`).

#### Userspace vHost

**Suggested use cases:** Virtual Appliance running Linux with an Intel® DPDK based application, or a legacy VirtIO based application.

* Option when applications are not trusted and highest small packet throughput required.
* Option when applications either do or do not need the Linux network stack.
* Single memcpy between guest and switch provides security.
* Single memcpy for guest to guest provides security.
* Option when using a modified QEMU version is not possible.

### Open vSwitch Datapath Features

Only a subset of the OpenFlow actions implemented in the original open source Open vSwitch application are currently supported by Intel® DPDK vSwitch.

Currently, the following are supported:

* `output`
* `drop`
* `set ethernet`
* `set IPv4`
* `set TCP`
* `set UDP`
* VLAN actions
  - `strip_vlan`
  - `mod_vlan_vid`
  - `mod_vlan_pcp`

Intel® DPDK vSwitch supports action sets, a.k.a. multiple actions.

______

## Code Modifications

### QEMU

QEMU 1.6.2 has been modified by adding a modified Inter-VM Shared Memory (IVSHMEM) PCI device that allows for an Intel® DPDK hugepage to be shared with an Intel® DPDK application in the guest. This allows for very fast zero-copy communication with the accelerated Open vSwitch. This code can be found in `qemu/hw/misc/ivshmem.c`.

### Open vSwitch

Open vSwitch 2.1.2 has been modified, adding a user-space dpif provider accelerated by the Intel® DPDK. This code is provided in `openvswitch/datapath/dpdk/`.

______

## Guest Applications

* `ovs_client` application: Sample application to demonstrate IVSHMEM method. This code is provided in `guest/ovs_client`.

More detailed information can be found in the [Sample Applications][doc-sample-apps] section.

______

## Some Notes on this Getting Started Guide

There are some things to note about this getting started guide:

* All code samples and commands have been manually wrapped at < 80 characters. This means they won't execute correctly if copied and pasted. Remove these line breaks, where they exist, before execution.
* Most, if not all, of the shell commands given within this guide require root access. Please execute as root or via `sudo`.

______

© 2014, Intel Corporation. All Rights Reserved

[ovs]: http://openvswitch.org/
[dpdk-ovs-github]: http://github.com/01org/dpdk-ovs
[qemu]: http://wiki.qemu.org/Main_Page
[doc-sample-apps]: 03_Sample_Applications.md
