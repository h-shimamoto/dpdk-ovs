To maximise throughput, assign individual cores to each of the various processes involved in the test setup. You can use either the `taskset` command or the core mask parameter passed to `ovs_client` and `ovs_dpdk` applications. Additionally, on the host, all available cores, with the exception of core 0, should be isolated from the kernel scheduler.

This section details a method to achieve maximum performance for an eight core system (sixteen logical cores if Intel® Hyper-Threading Technology enabled).

______

## Isolate Cores

In the host, edit `/boot/grub2/grub.cfg` (or `/etc/default/grub`, if applicable), specifically this line:

```
GRUBCMDLINELINUX="..."
```

Include the following:

```
isolcpus=1,2,...,n
```

**Note:** `n` should be the max number of physical cores available if Intel® Hyper-Threading Technology is enabled. We recommend that it is disabled. **Always leave core 0 for the operating system**.

Update the grub configuration file:

```bash
grub2-mkconfig -o /boot/grub2/grub.cfg
```

You must then reboot your system for these changes to take effect.

______

## Affinitising the Host Cores

A DPDK application is implicitly affinitised, as prescribed by the `CORE_MASK` argument supplied on the application's command-line.
**Note:** For all Intel® DPDK-enabled applications, the core mask option (`-c`) must be set such that no two processes have overlapping core masks - failure to do so could result in undesired behaviour, and performance degradation due to corruption of an lcore's local mbuf cache.

Non-DPDK processes may be affinitised using `taskset`.

Sample host-process core affinity:

| Process | Core | Core Mask | Comments |
|:-------:|:----:|:---------:|:--------:|
|Kernel           | 0 | 0x1 |All other CPUs isolated (`isolcpus` boot parameter)|
|ovs-dpdk process | 2 | 0x4 |Affinity set in `ovs-dpdk` command line |
|ovs-dpdk process | 3 | 0x8 |Affinity set in `ovs-dpdk` command line |
|QEMU process VM1 VMCPU0 | 4 | 0x10 |`taskset -a <pid_of_qemu_process>` |
|QEMU process VM1 VMCPU1 | 5 | 0x20 |`taskset -a <pid_of_qemu_process>` |
|QEMU process VM2 VMCPU0 | 6 | 0x40 |`taskset -a <pid_of_qemu_process>` |
|QEMU process VM2 VMCPU1 | 7 | 0x80 |`taskset -a <pid_of_qemu_process>` |

______

## Userspace vHost Tuning

As vHost has small buffers it can be heavily affected by packet drops. To help mitigate this, you can change the number of times the userspace vHost port will retry before dropping. You may also need to change the number of retries which the application in the guest will attempt as well as the host ovs-dpdk application's values.


###On The Host
Open `openvswitch/datatpath/dpdk/ovdk_vport_vhost.c`

And make the following change:
```diff
- OVDK_VHOST_RETRY_NUM	4
+ OVDK_VHOST_RETRY_NUM	64
```

_Note: The values which are provided are samples, you will need to test to see which values are optimal for your system_
_Note: You will have to recompile Open vSwitch after making this change._

###On The Guest
We use the `test_pmd` DPDK sample application here as an example as it is also used in the [example Userspace vHost configuration][doc-sample-vhost]. This step should be done before building the test-pmd application, or the test-pmd application should be rebuilt after this change has been made.

Open `$RTE_SDK/app/test-pmd/macfwd-retry.c` on the guest.

And make the following change:
```diff
-#define BURST_TX_WAIT_US 10
-#define BURST_TX_RETRIES 5
+#define BURST_TX_WAIT_US 15
+#define BURST_TX_RETRIES 512
```

_Note: The values which are provided are samples, you will need to test to see which values are optimal for your system_

______

## IVSHM Tuning

Depending on the speed of your virtual machines, you may also need to tune IVSHM. 

The simplest ways to do this are:

1. Increase the buffer size
2. Increase the number of retries on the free queue.

###Increase the buffer size
To increase the buffer size for IVSHM you will need to change the following #defines in `openvswitch/datapath/dpdk/ovdk_vport_client.c`.

```C
#define PORT_CLIENT_RX_RING_SIZE       4096$
#define PORT_CLIENT_TX_RING_SIZE       4096$
#define PORT_CLIENT_FREE_RING_SIZE     4096$
#define PORT_CLIENT_ALLOC_RING_SIZE    512$
```

It is important to note when changing these values that the alloc ring is kept constantly full, and it used by the vSwitch itself for communication with the datapath. Thus, it should be kept as small as possible to avoid unnecessary memory hogging.

### Increase the number of/interval between retries on the free queue

To increase the number of retries and the time between retries, you will need to change the following two `#defines` in `openvswitch/datapath/dpdk/rte_port_ivshm.c`.

```C
#define IVSHM_BURST_TX_WAIT_US  15
#define IVSHM_BURST_TX_RETRIES  256
```

As before, this is a compile time parameter, so the code will need to be recompiled after your change is made.
______

© 2014, Intel Corporation. All Rights Reserved

[doc-sample-vhost]: 04_Sample_Configurations/02_Userspace-vHost.md
