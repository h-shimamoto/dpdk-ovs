There are a small number of long-term issues, related to both the general design of Intel® DPDK vSwitch and outstanding bugs. These are detailed below.

______

## Intel® DPDK vSwitch

* To view a list of known bugs, or log a new one, please visit the [Issues][ovdk-issues] section of the Intel® DPDK vSwitch GitHub page.

* This release supports Intel® DPDK v1.7.1 only. Intel® DPDK v1.7.0 exhibits issues with IVSHMEM and is therefore unsupported.

* Intel® Virtualization Technology for Directed I/O (Intel® VT-d) should be disabled in the BIOS settings, unless PCI passthrough is required, in which case the following options should be added to the kernel boot parameters:

    ```
    intel_iommu=on iommu=pt
    ```

* When starting the VMs, the following warning may appear:

    ```bash
    (ASLR) is enabled in the kernel. This may cause issues with mapping memory into secondary processes.
    ```

    Although in most cases this warning is harmless, to suppress it, run the following command:

    ```bash
    echo 0 > /proc/sys/kernel/randomize_va_space
    ```

* This release has not been tested or validated for use with Virtual Functions, although it should theoretically work with Intel® DPDK 1.7.0.

* If testing performance with TCP, variances in performance may be observed; this variation is due to the protocol's congestion-control mechanisms. UDP produces more reliable and repeatable results, and it is the preferred protocol for performance testing.

* On start-up, Intel® DPDK vSwitch may issue an error:

    ```bash
    EAL: memzone_reserve_aligned_thread_unsafe(): memzone
    <RG_MP_log_history> already exists
    RING: Cannot reserve memory
    ```

    When an Intel® DPDK process starts, it attempts to reserve memory for various rings through a call to `rte_memzone_reserve`; in the case of a Intel® DPDK primary process, the operation should succeed, but for a secondary process, it is expected to fail, as the memory has already been reserved by the primary process. The particular ring specified in the error message - `RG_MP_log_history` - does not affect operation of the secondary process, so this error may be disregarded.

* On start-up, `ovs-dpdk` may complain that no ports are available (when using an Intel® DPDK-supported NIC):

    ```bash
    Total ports: 0

    EAL: Error - exiting with code: 1
    Cause: Cannot allocate memory for port tx_q details
    ```

    These error messages indicate that Intel® DPDK initialization failed because it did not detect any recognized physical ports. One possible cause is that the NIC is still driven by the default ixgbe driver. To resolve this issue, run `DPDK/tools/dpdk_nic_bind.py` before starting ovs-dpdk. (This process lets the Intel® DPDK poll mode driver take over the NIC.)

    For example, `dpdk_nic_bind.py -b igb_uio <PCI ID of NIC port>` binds the NIC to the Intel® DPDK igb_uio driver.

* Issue present in some kernels (3.15.7 to 3.16.6 inclusive):

    When an RFC2544 throughput test is run on the setup described in [vHost Sample Configurations][doc-vhost-setup], using 2 guests, it shows excessive packet loss, even at low throughput. This degradation in performance was introduced on these kernels by the commit ```6daca6a55794bd21f51a643c064417e56f581d31```. There are two potential solutions for this issue. The first is to configure your setup with ports which interact together (such as PHY RX and vHost TX) on the same core, this should improve RFC2544 throughput. A second solution is to upgrade the kernel to 3.16.7. This may help but is not guaranteed to resolve the issue. A fresh OS install with either the 3.16.7 kernel or the 3.15.6 kernel is best.

* Non IP packets not handled in the datapath:

    By design, flow keys are not initialized in the datapath for non IP packets. Instead an upcall is created and non IP packets are handled at the vswitchd level (slowpath).

* False statistics reported for failed packet transmissions:

    Statistics for packets will be increased before a packet transmit is handled. If the packet transmit subsequently fails, the transmit statistic will still increment. This is related to the order in which rte_pipeline_port_out_packet_insert executes the transmit actions.

* QEMU memory size affects IVSHM guest access method:

    When using the IVSHM guest access method memory can be shared incorrectly to the guest. This has been observed when using the ATOM platform with 2MB hugpages and where guest memory is greater than 3GB.

* Traffix TX failing for multiple source IPs when used in conjuction with multiple bridges and the NORMAL action:

    This behavior has been observed in the following setup. 2 bridges patched back to back, "br1" and "br2". 1 physical port installed on "br1". 1 US-vHost port installed on "br2". Two flows are installed on "br1", the first flow executes the NORMAL action. It should route ingress traffic from the physical device to "br2". The second flow should modify the vlan on egress traffic before executing the NORMAL action and transmitting from the physical device. Similarly two flows are installed on "br2", the first flow modifies the vlan on an ingress packet received from "br1" and the NORMAL action is executed to forward traffic to the guest via the US-vHost port. The second flow executes the NORMAL action on egress traffic received from the US-vHost port and should forward traffic to "br1". The expected traffic route is to enter "br1" via the physical port. Traffic should be forwarded via the NORMAL action to "br2" where the vlan is modified and forwarded to a guest via the US-vHost port on "br2". Traffic should then be forwarded from the guest back through the US-vHost device to "br2", forwarded via the NORMAL action back to "br1" and finally the vlan modified to be to its original value before being transmitted back out the physical device on "br1". Using a single source and destination address for IP traffic this works as expected. However if multiple source addresses are used, it has been observed that traffic does not transmit from the physical device on "br1" as expected.

______

## Intel® DPDK vSwitch Sample Guest Application

In the current IVSHM implementation, multiple Intel® DPDK objects (memory zones, rings, and memory pools) can be shared between different guests. The host application determines what to share with each guest. The guest applications are not Intel® DPDK secondary processes anymore, and so they can create their own Intel® DPDK objects as well.

______

## Open vSwitch Testsuite

Open vSwitch contains a number of unit tests that collectively form the OVS "testsuite". While the majority of these tests currently pass without issue, a small number do fail. The common cause of failure for these tests is a discrepancy in the command line arguments required by many of the utilities in standard Open vSwitch and their equivalents in Intel® DPDK vSwitch. In addition, test three (3) causes the testsuite to hang and should be skipped. These issues will be resolved in a future release.

Many of the tests also fail due to differences in the required parameters for utilities such as `ovs-dpctl` (that is, Intel® DPDK vSwitch's version of these utilities require EAL parameters). As a result, these tests should be used as guidelines only.

In addition to the standard unit tests, Intel® DPDK vSwitch extends the testsuite with a number of "Intel® DPDK vSwitch"-specific unit tests. These tests require root privileges to run, due to the use of hugepages by the Intel® DPDK library. These tests are currently the only tests guaranteed to pass, with the exception of the `port-del` and `multicore-port-del` unit tests, as described earlier in this document.

______

## ovs-vswitchd

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-vsctl

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-ofctl

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-dpctl

Not all functionality that is supported by Open vSwitch is supported by the Intel® DPDK vSwitch.

______

## ovs-ivshm-mngr

The IVSHM manager utility must be executed once the switch is up and running and not before. An attempt to share Intel® DPDK objects using the IVSHM manager utility before the switch has finished with its setup/init process may cause undesired behavior.

______

© 2014, Intel Corporation. All Rights Reserved

[ovdk-issues]: https://github.com/01org/dpdk-ovs/issues
[doc-vhost-setup]: 04_Sample_Configurations/02_Userspace-vHost.md
