Intel® DPDK vSwitch consists of multiple components. Each of these must be downloaded and compiled in order to run any of the sample configurations detailed later.

______

## Build Requirements

To compile Intel® DPDK vSwitch for stock Fedora 18/20, you will need the following software:

* `gcc`
* `kernel-devel-$(uname -r)`
* `kernel-modules-extra-$(uname -r)`
* `autoconf`
* `automake`
* `libtool`
* `fuse-devel`
* `pixman-devel`
* `zlib-devel`
* `glibc.i686`
* `glib2-devel`
* `pciutils` (for Intel DPDK's `dpdk_nic_bind.py` script)
* `rsvg-convert` (to update PNGs generated from SVGs in `docs`)

In addition, one or more of the following packages may be required:

* `make`
* Additional GNU Autotools, i.e. `autom4te`
* `patch`
* `glib-devel`
* `expect`
* `fuse`
* `openssl`
* `libfdt-devel`

______

## Download

### Intel® DPDK

Intel® DPDK is a set of software libraries and Ethernet drivers (native and virtualized) that run in Linux user space to boost packet processing throughput on Intel® Architecture. You can find information, including download links, for Intel® DPDK via [DPDK.org][dpdkorg].

### Intel® DPDK vSwitch

Intel® DPDK vSwitch (along with supporting applications) can be sourced in two different ways:

* By cloning or downloading this repository
* By downloading the latest stable release from [01.org][01org-downloads]

This guide will assume the latter option has been chosen: please adjust the provided commands accordingly.

______

## Extract

Before compiling, extract the source files for Intel® DPDK and Intel® DPDK vSwitch. This can be done as follows:

```bash
mkdir ~/ovs_dpdk
cd ~/ovs_dpdk
mkdir ovdk
tar -xzvf <dpdk_release_pkg>.tar.gz
tar -xvzf <dpdk_vswitch_release_pkg>.tar.gz -C ovdk
mv <dpdk_extract_dir> DPDK
```

______

## Compile

There are two possible approaches to compiling Intel® DPDK vSwitch, its supporting applications and its dependencies: *automatically*, via the provided top-level makefile, or manually.

### Top-level Makefile

Intel® DPDK vSwitch provides a top-level makefile capable of building each component in the correct order and fashion. This makefile provides a number of targets:

* `all`
* `config`
* `clean`
* `check`
* `xxx`, where `xxx` is one of the following: `ovs`, `dpdk`, `qemu`
* `xxx-deps`, where `xxx` is one of the following: `ovs`, `qemu`, `ivshm`
* `config-xxx`, where `xxx` is one of the following: `ovs`, `dpdk`, `qemu`
* `clean-xxx`, where `xxx` is one of the following: `ovs`, `dpdk`, `qemu`

A typical execution of this would look like:

```bash
cd ~/ovs_dpdk               # directory containing DPDK and DPDK vSwitch
export RTE_SDK=$(pwd)/DPDK  # DPDK sub-directory
cd ovdk                     # DPDK vSwitch sub-directory
make config && make
```

**Note:** Intel® DPDK vSwitch has been validated in a limited number of environments, and may be subject to portability issues. Please follow the manual steps below if you encounter such issues.

### Manually

Three different components are necessary to run Intel® DPDK vSwitch: Intel® DPDK, QEMU and the applications of Intel® DPDK vSwitch. Of these, Intel® DPDK must be built first due to dependencies in the latter applications.

Each of these commands should be run from the top-level directory in which you previously extracted all dependencies, i.e.:

```bash
cd ~/ovs_dpdk               # directory containing DPDK and DPDK vSwitch
```

#### DPDK

Refer to the [Intel® DPDK Getting Started Guide][dpdkorg-dpdkgsg] for a relevant make target, e.g.:

```bash
cd DPDK                     # DPDK sub-directory
export RTE_SDK=$(pwd)
export RTE_TARGET="x86_64-ivshmem-linuxapp-gcc"
make CONFIG_RTE_BUILD_COMBINE_LIBS=y CONFIG_RTE_BUILD_SHARED_LIB=y install T="$RTE_TARGET"
cd -
```

**Note:** Generally speaking, Intel® DPDK has to be compiled using the `ivshmem` target. However, this requirement may differ depending on the environment in which the system is built. Please refer to sections two and three of the *Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide* - which can be found on [dpdk.org][dpdkorg-dpdkgsg] - for additional information on performing this step.

#### Open vSwitch

```bash
cd ovdk/openvswitch         # DPDK vSwitch sub-directory
./boot.sh
./configure RTE_SDK=$(RTE_SDK) --disable-ssl
make
cd -
```

#### QEMU

```bash
cd ovdk/qemu
./configure --enable-kvm --target-list=x86_64-softmmu
cd -
```

______

## Environment Setup

Following successful compilation, a number of final steps may be necessary in order to run Intel® DPDK vSwitch. Each of these steps *may not be necessary*, and should be run/skipped accordingly. Additionally, many of these commands may need to be executed each time the board is rebooted. Therefore, please keep this section a *reference guide*.

Please note, that as with the previous section, each of these commands should be run from the top-level directory in which you previously extracted all dependencies, i.e.:

```bash
cd ~/ovs_dpdk
```

### Configure Kernel Boot Parameters

Intel® DPDK vSwitch requires hugepages be enabled. Multiple 1 GB and 2 MB hugepages are supported; however, standard Intel® DPDK vSwitch configuration requires a minimum of one 1 GB hugepage per instance of the vSwitch. This can be enabled by adding the following to the kernel boot parameters:

```bash
default_hugepagesz=1G hugepagesz=1G hugepages=1
```

Userspace-vHost ports may require multiple hugepages. For example, if using a VM with 2 GB of memory and giving the switch 2 GB memory, we need to allocate eight hugepages. This ensures that we have the four hugepages required on the same socket. To enable these eight 1 GB hugepages:

```bash
default_hugepagesz=1G hugepagesz=1G hugepages=8
```

2 MB pages may also be used:

```bash
default_hugepagesz=2M hugepagesz=2M hugepages=2048
```

Following this, it will be necessary to reboot your device. Please do so before continuing.

Please note that Intel® DPDK vSwitch requires hugepages in order to run. If your device does not support these, Intel® DPDK vSwitch will be unable to execute. You can check support using the [Intel® ARK Tool][intel-ark].

### Create Required Directories

You may need to manually create some directories if you have not previously run either Intel® DPDK vSwitch or stock Open vSwitch, or have deleted prior versions of the directories:

```bash
mkdir -p /usr/local/etc/openvswitch
mkdir -p /usr/local/var/run/openvswitch
```

### Mount Huge Pages

Hugepages may or may not be mounted on your system. You can check this as follows:

```bash
mount | grep /dev/hugepages
```

If this returns no results, mount the hugepages like so:

```bash
mkdir -p /dev/hugepages
mount -t hugetlbfs nodev /dev/hugepages
```

### Insert `igb_uio` Driver & Bind PCI Devices

While Intel® DPDK vSwitch provides a large number of different paths to guests, it still requires physical Network Interface Cards (NICs), bound to the `igb_uio` driver, to function correctly. You can check if this driver is already loaded as follows:

```bash
lsmod | grep igb_uio
```

If this returns no results, insert the driver like so:

```bash
modprobe uio
insmod ./DPDK/$RTE_TARGET/kmod/igb_uio.ko
```

Once the `igb_uio` driver has been loaded, some interfaces should be bound to it. Intel® DPDK does not do this automatically  so you should use the `dpdk_nic_bind.py` script found in `$RTE_SDK/tools/`. In brief, the following commands can be used:

To get the current status of the ports (i.e. what's bound and what isn't):

```bash
./DPDK/tools/dpdk_nic_bind.py --status
```

To bind devices:

```bash
./DPDK/tools/dpdk_nic_bind.py --bind igb_uio [hw_addr...]
```

For example:

```bash
./DPDK/tools/dpdk_nic_bind.py --bind igb_uio 81:00.0 81:00.1
```

You can refer to the [*Intel® Data Plane Development Kit (Intel DPDK) - Getting Started Guide*][dpdkorg-dpdkgsg] - for full instructions on performing this step.

### Insert the `rte_kni` Driver

One of the interfaces found in Intel® DPDK vSwitch - vEth - relies on the Intel® DPDK KNI kernel module. Unless this interface is disabled at compile time, you must insert the KNI kernel module for `ovs-dpdk` to start successfully. You can check if this module is already loaded as follows:

```bash
lsmod | grep rte_kni
```

If this returns no results, insert the module like so:

```bash
insmod ./DPDK/$RTE_TARGET/kmod/rte_kni.ko
```

______

© 2014, Intel Corporation. All Rights Reserved

[01org-downloads]: https://01.org/packet-processing/downloads
[dpdkorg]: http://dpdk.org
[dpdkorg-dpdkgsg]: http://dpdk.org/doc
[intel-ark]: http://ark.intel.com/
