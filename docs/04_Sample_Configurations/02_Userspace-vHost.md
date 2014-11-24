Intel® DPDK vSwitch supports the offload of virtio-net device servicing in the guest, reducing context switching and packet copies in the virtual dataplane when using the vhost-net module.

This section contains instructions on how to compile and run a sample application that demonstrates performance of an Intel® DPDK-accelerated version of Userspace vHost for IO virtualization. For more information on the Userspace vHost implementation refer to the [*Intel® Data Plane Development Kit (Intel DPDK) - Sample Applications User Guide*][dpdkorg-dpdksample].

## Overview

This guide covers a Phy->VM->Phy configuration using Userspace vHost vports:

```
                                                         __
    +---------------------------------------------------+   |
    |   guest                                           |   |
    |   +-------------------------------------------+   |   |
    |   |                  TESTPMD                  |   |   |
    |   +-------------------------------------------+   |   |
    |       ^                                  :        |   |
    |       |                                  |        |   |  guest
    |       :                                  v        |   |
    |   +-------------------------------------------+   |   |
    |   |                   DPDK                    |   |   |
    +---+---------------+-----------+---------------+---+ __|
            ^                                  :
            |                                  |
            :                                  v         __
    +---+---------------+----------+--------------=+---+   |
    |   | vhost vport 0 |          | vhost vport 1 |   |   |
    |   +---------------+          +---------------+   |   |
    |       ^      |                    ^      :       |   |
    |       |      +-=------------------+      |       |   |  host
    |       :                                  v       |   |
    |   +--------------+            +--------------+   |   |
    |   |   phy port   |  ovs-dpdk  |   phy port   |   |   |
    +---+--------------+------------+--------------+---+ __|
               ^                           :
               |                           |
               :                           v
    +--------------------------------------------------+
    |                                                  |
    |                traffic generator                 |
    |                                                  |
    +--------------------------------------------------+
```

______


## Prerequisites

There are some extra requirements necessary in order to utilise US-vHost devices in Intel® DPDK
vSwitch.

### Assert correct number of hugepages present

Hugepages are required for both the guest and the vSwitch application. Ideally there should be enough hugepages on a single socket to maximise performance. For example, to run a guest with 2 GB of memory and running the vSwitch with 2 GB of memory there should be eight 1 GB hugepages allocated, i.e. four on each socket.

### Insert the CUSE kernel module

```bash
modprobe cuse
```

**Note:** If this fails, you likely don't have `kernel-modules-extra` installed. Please review the [Installation Guide][doc-installation] for more information on required packages.

### Insert the fd_link module

```bash
insmod ./openvswitch/datapath/dpdk/fd_link/fd_link.ko
```

### Remove vhost-net module

The default configuration will use the `/dev/vhost-net` character device. This device is also used for legacy vHost and so it is important that this module is removed if it is being used.

```bash
rmmod vhost-net
```

If the module has been removed but the device `/dev/vhost-net` still exists then it should be removed manually. Note that this may exist on startup, even without the legacy vHost module being present.

```bash
rm /dev/vhost-net
```

### Install fuse library

Intel® DPDK vSwitch now includes the `fuse` library at compile time. This library must exist on the system or the application will fail to compile. This can be installed through the distributions repository or it can be [built from source][fuse-source].

______

## Initial Switch Setup

On the host, remove any configuration associated with a previous run of the switch:

```bash
pkill -9 ovs
rm -rf /usr/local/var/run/openvswitch/
rm -rf /usr/local/etc/openvswitch/
mkdir -p /usr/local/var/run/openvswitch/
mkdir -p /usr/local/etc/openvswitch/
rm -f /tmp/conf.db
```

Configure some environment variables:

```bash
cd openvswitch
export OPENVSWITCH_DIR=$(pwd)
```

Initialise the Open vSwitch database server:

```bash
./ovsdb/ovsdb-tool create /usr/local/etc/openvswitch/conf.db \
  $OPENVSWITCH_DIR/vswitchd/vswitch.ovsschema
./ovsdb/ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
  --remote=db:Open_vSwitch,Open_vSwitch,manager_options &
```

Add a bridge to the switch:

```bash
./utilities/ovs-vsctl --no-wait add-br br0 -- set Bridge br0 datapath_type=dpdk
```

Add ports to the bridge:

```bash
./utilities/ovs-vsctl --no-wait add-port br0 port1 -- set Interface port1 \
  type=dpdkphy ofport_request=1 option:port=0
./utilities/ovs-vsctl --no-wait add-port br0 port2 -- set Interface port2 \
  type=dpdkphy ofport_request=2 option:port=1
./utilities/ovs-vsctl --no-wait add-port br0 port3 -- set Interface port3 \
  type=dpdkvhost ofport_request=3
./utilities/ovs-vsctl --no-wait add-port br0 port4 -- set Interface port4 \
  type=dpdkvhost ofport_request=4
```

Note the additional `option` parameter required for `dpdkphy` type ports.

Confirm the ports have been successfully added:

```bash
./utilities/ovs-vsctl show
```

You should see something like this:

```bash
00000000-0000-0000-0000-000000000000
    Bridge "br0"
        Port "br0"
            Interface "br0"
                type: internal
        Port "port1"
            Interface "port1"
                type: dpdkphy
                options: {port="0"}
        Port "port2"
            Interface "port2"
                type: dpdkphy
                options: {port="1"}
        Port "port3"
            Interface "port3"
                type: dpdkvhost
        Port "port4"
            Interface "port4"
                type: dpdkvhost
```

Start `ovs-dpdk`:

```bash
./datapath/dpdk/build/ovs-dpdk -c 0x0F -n 4 --proc-type primary \
  --base-virtaddr=<virt_addr> --socket-mem 2048,2048 -- \
  --stats_core=0 --stats_int=5 -p 0x03
```

Note the additional `--socket-mem` option. This ensures the DPDK app does not use all available hugepage memory on the system.

Start the Open vSwitch daemon:

```bash
./vswitchd/ovs-vswitchd -c 0x100 --proc-type=secondary -- \
  --pidfile=/tmp/vswitchd.pid
```

______

## Flow Table Setup

Delete the default flow entries from the bridge:

```bash
./utilities/ovs-ofctl del-flows br0
```

Add flow entries to switch packets from `port1` (Phy 0) to `port3` (vHost 0) on the ingress path, and from `port4` (vHost 1) to `port3` (Phy 1) on the egress path:

```bash
./utilities/ovs-ofctl add-flow br0 in_port=1,dl_type=0x0800,nw_src=10.1.1.1,\
nw_dst=10.1.1.254,idle_timeout=0,action=output:3
./utilities/ovs-ofctl add-flow br0 in_port=4,dl_type=0x0800,nw_src=10.1.1.1,\
nw_dst=10.1.1.254,idle_timeout=0,action=output:2
```

______

## Guest Setup

### Copy Files

Intel® DPDK source code must be copied to each guest required. The simplest way to do this step is by copying the required files to a directory on the host and mounting this directory as a drive on the guest. Once the guest is started, the files can be copied from the mounted drive to a local directory. This method has been validated using qcow2 images.

```bash
rm -rf /tmp/qemu_share
mkdir -p /tmp/qemu_share
mkdir -p /tmp/qemu_share/DPDK
chmod 777 /tmp/qemu_share
cp -aL <DPDK_DIR>/* /tmp/qemu_share/DPDK
```

## Create VM

**Note:** QEMU will fail if `ovs-dpdk` is not already running. The following command line will launch QEMU with two vhost enabled virtio devices. It is important to note that the tap device names should be **identical** to the ovs-dpdk port names:

```bash
./qemu/x86_64-softmmu/qemu-system-x86_64 -cpu host -boot c                   \
  -hda <qemu_imagename.qcow2> -snapshot -m 4096M -smp 2                      \
  --enable-kvm -name 'client 1' -nographic -vnc :1 -pidfile /tmp/vm1.pid     \
  -drive file=fat:rw:/tmp/qemu_share,snapshot=off                            \
  -monitor unix:/tmp/vm1monitor,server,nowait                                \
  -net none -no-reboot -mem-path /dev/hugepages -mem-prealloc                \
  -netdev type=tap,id=net1,script=no,downscript=no,ifname=port3,vhost=on     \
  -device virtio-net-pci,netdev=net1,mac=00:00:00:00:00:01,csum=off,gso=off,\
guest_tso4=off,guest_tso6=off,guest_ecn=off                                  \
  -netdev type=tap,id=net2,script=no,downscript=no,ifname=port4,vhost=on     \
  -device virtio-net-pci,netdev=net2,mac=00:00:00:00:00:02,csum=off,gso=off,\
guest_tso4=off,guest_tso6=off,guest_ecn=off
```

**Note:** Userspace vHost devices can also be launched with an unmodified QEMU version by excluding the Intel® DPDK related options before the "--".

______

## Guest Configuration

The following configuration must be performed on each vHost client.

### Copy Files

Login to the VM and copy across the files:

```bash
mkdir –p /mnt/vhost
mkdir –p /root/vhost
mount –o iocharset=utf8 /dev/sdb1 /mnt/vhost
cp –a /mnt/vhost/* /root/vhost
```

### Build DPDK on the Guest

```bash
cd /root/vhost/DPDK
export RTE_SDK=$(pwd)
export RTE_TARGET=x86_64-ivshmem-linuxapp-gcc
make uninstall
make install T=x86_64-ivshmem-linuxapp-gcc
```

### Build `test-pmd` App

```bash
cd /root/vhost/DPDK/app/test-pmd
make clean
make
```

### Insert `igb_uio` Module and Bind Devices

Once everything has been built, insert the `igb_uio` driver:

```bash
modprobe uio
insmod /root/vhost/DPDK/x86_64-ivshmem-linuxapp-gcc/kmod/igb_uio.ko
```

Once complete, bind the necessary vHost devices to this driver:

```bash
/root/vhost/DPDK/tools/dpdk_nic_bind.py -b igb_uio 0000:00:03.0 0000:00:04.0
```

### Execute `test-pmd` App

Having completed the above, run the `test-pmd` application.

```bash
./testpmd -c 0x3 -n 4 --socket-mem 128 -- --burst=64 -i --txqflags=0xf00
```

At the `testpmd` prompt, issue the following commands:

```bash
set fwd mac_retry
start
```

______

## Run the Test

Start transmission of packets from the packet generator. If setup correctly, packets can be seen returning to the transmitting port from the DUT.

______

© 2014, Intel Corporation. All Rights Reserved

[doc-installation]: 01_Installation.md
[dpdkorg-dpdksample]: http://dpdk.org/doc
[fuse-source]: http://sourceforge.net/projects/fuse/files/fuse-2.X/
