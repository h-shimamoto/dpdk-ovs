/*
 * Copyright 2012-2014 Intel Corporation All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <config.h>

#include <rte_config.h>

#include "ovs-thread.h"
#include "dpif-dpdk-vport-table.h"
#include "datapath/dpdk/ovdk_vport_types.h"

#define DPIF_DPDK_VPORT_FILENAME "/tmp/dpif_dpdk_vport_table"
#define DPIF_DPDK_VPORT_FILESIZE (sizeof(struct dpif_dpdk_vport_table))
#define DPIF_DPDK_VPORT_TABLE_PERMISSIONS (S_IRUSR | S_IWUSR | S_IXUSR | \
                                           S_IRGRP | S_IXGRP | \
                                           S_IROTH | S_IXOTH)

struct dpif_dpdk_vport_table_entry {
    bool in_use;
    unsigned lcore_id;
    enum ovdk_vport_type type;
    char name[OVDK_MAX_VPORT_NAMESIZE];
};

struct dpif_dpdk_vport_table {
    struct dpif_dpdk_vport_table_entry entry[OVDK_MAX_VPORTS];
};

static struct dpif_dpdk_vport_table *vport_table = NULL;
static int vport_table_fd = -1;
static struct ovs_mutex dpif_dpdk_vport_table_mutex = OVS_MUTEX_INITIALIZER;

/*
 * Create the vport table.
 *
 * The port table is stored in a memory mapped file to allow it to persist if
 * the vswitchd is not running. The following function creates that file and
 * initializes the port table.
 *
 * Returns 0 on success, or -errno if any of the file operations fail.
 */
int
dpif_dpdk_vport_table_construct(void)
{
    int ret = -1;
    int i = 0;

    vport_table_fd = open(DPIF_DPDK_VPORT_FILENAME, O_RDWR | O_CREAT,
            DPIF_DPDK_VPORT_TABLE_PERMISSIONS);
    if (vport_table_fd == -1) {
        return -errno;
    }

    ret = lseek(vport_table_fd, DPIF_DPDK_VPORT_FILESIZE, SEEK_SET);
    if (ret == (DPIF_DPDK_VPORT_FILESIZE - 1)) {
        return -errno;
    }

    ret = write(vport_table_fd, "", 1);
    if (ret == -1) {
        return -errno;
    }

    vport_table = (struct dpif_dpdk_vport_table *)(mmap(0,
            DPIF_DPDK_VPORT_FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
            vport_table_fd, 0));
    if (vport_table == MAP_FAILED) {
        close(vport_table_fd);
        remove(DPIF_DPDK_VPORT_FILENAME);
        return -errno;
    }

    ovs_mutex_init(&dpif_dpdk_vport_table_mutex);

    for (i = 0; i < OVDK_MAX_VPORTS; i++) {
        vport_table->entry[i].in_use = false;
        vport_table->entry[i].lcore_id = UINT32_MAX;
    }

    return 0;
}

/*
 * Destroy the vport table.
 *
 * The port table is stored in a memory mapped file to allow it to persist if
 * the vswitchd is not running. The following function destroys that file.
 *
 * Returns 0 on success, or -errno if any of the file operations fail.
 */
int
dpif_dpdk_vport_table_destroy(void)
{
    int err = 0;

    err = dpif_dpdk_vport_table_close();

    if (remove(DPIF_DPDK_VPORT_FILENAME) == -1) {
        err = -errno;
    }

    ovs_mutex_destroy(&dpif_dpdk_vport_table_mutex);

    return err;
}

/*
 * Open the vport table.
 *
 * If the vport table has already been constructed, this function will open
 * the file which backs the table and remap the memory.
 *
 * Returns 0 on success, or -errno if any of the file operations fail.
 */
int
dpif_dpdk_vport_table_open(void)
{
    vport_table_fd = open(DPIF_DPDK_VPORT_FILENAME, O_RDWR | O_CREAT,
            DPIF_DPDK_VPORT_TABLE_PERMISSIONS);
    if (vport_table_fd == -1) {
        return -errno;
    }

    vport_table = (struct dpif_dpdk_vport_table *)(mmap(0,
        DPIF_DPDK_VPORT_FILESIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED, vport_table_fd, 0));
    if (vport_table == MAP_FAILED) {
        return -errno;
    }

    return 0;
}

/*
 * Close the vport table.
 *
 * This function will close the vport table without destroying the file
 * that backs it.
 *
 * Returns 0 on success, or -errno if any of the file operations fail.
 */
int
dpif_dpdk_vport_table_close(void)
{
    int err = 0;

    /* If a memory unmap fails, we still want to continue. Perhaps only
     * the unmapping has failed and the file still exists */
    if (munmap(vport_table, DPIF_DPDK_VPORT_FILESIZE) == -1) {
        err = -errno;
    }

    /* Check if file is open before trying to close it */
    if(fcntl(vport_table_fd, F_GETFD) != -1 && errno != EBADF) {
        if (close(vport_table_fd) == -1) {
            err = -errno;  /* Keep only the most recent error code */
        }
    }

    return err;
}

/*
 * Check if the vport table exists and is accessible.
 *
 * Returns 0 on success, or -errno if file does not exist or is inaccessible.
 */
int
dpif_dpdk_vport_table_exists(void)
{
    int err = 0;

    /* Check file exists */
    if(access(DPIF_DPDK_VPORT_FILENAME, F_OK) == -1) {
        return -errno;  /* no point checking remaining options */
    }

    /* Check read/write access */
    if(access(DPIF_DPDK_VPORT_FILENAME, R_OK | W_OK ) == -1) {
        err = -errno;
    }

    return err;
}

/*
 * Add an entry to the vport table.
 *
 * This function will add an entry to the vport table with an lcore of
 * 'lcore_id' and a name of 'name'.
 *
 * If the caller requests a vportid through the 'req_vportid'
 * parameter, this function will attempt to add the entry with this vportid.
 * If the function is unable to add an entry with this vportid or if
 * 'req_vportid' is not specified (by setting it to OVDK_MAX_VPORTS), the
 * function will search for a vportid of type 'type'.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters. If it is unable to
 * add the entry, returns -ENOENT.
 */
int
dpif_dpdk_vport_table_entry_add(enum ovdk_vport_type type, unsigned *lcore_id,
                                const char *name, uint32_t *req_vportid)
{
    uint32_t start_vportid = 0;
    uint32_t end_vportid = 0;
    uint32_t vportid = 0;
    int ret = -ENOENT;

    if (type >= OVDK_VPORT_TYPE_MAX) {
        return -EINVAL;
    }

    if (lcore_id == NULL || *lcore_id >= RTE_MAX_LCORE) {
        return -EINVAL;
    }

    if (name == NULL) {
        return -EINVAL;
    }

    if (req_vportid == NULL || *req_vportid > OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    /* Determine the range of 'vportid' for this 'type' */
    start_vportid = type;

    switch (type) {
    case OVDK_VPORT_TYPE_PHY:
        end_vportid = start_vportid + OVDK_MAX_PHYPORTS;
        break;
    case OVDK_VPORT_TYPE_CLIENT:
        end_vportid = start_vportid + OVDK_MAX_CLIENTS;
        break;
    case OVDK_VPORT_TYPE_VHOST:
        end_vportid = start_vportid + OVDK_MAX_VHOSTS;
        break;
    case OVDK_VPORT_TYPE_KNI:
        end_vportid = start_vportid + OVDK_MAX_KNIS;
        break;
    case OVDK_VPORT_TYPE_BRIDGE:
        end_vportid = start_vportid + OVDK_MAX_BRIDGES;
        break;
    case OVDK_VPORT_TYPE_VETH:
        end_vportid = start_vportid + OVDK_MAX_VETHS;
        break;
    case OVDK_VPORT_TYPE_MEMNIC:
        end_vportid = start_vportid + OVDK_MAX_MEMNICS;
        break;
    case OVDK_VPORT_TYPE_VSWITCHD:
    case OVDK_VPORT_TYPE_DISABLED:
    case OVDK_VPORT_TYPE_MAX:
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);

    /*
     * If 'req_vportid' is specified then we try to get that vportid.
     * We check if the existing entry for lcore_id is equal to UINT32_MAX,
     * if so, this means the vport has not been used before. If it does
     * not equal UINT32_MAX then the vportid has been used before,
     * we set the *lcore_id to that of the existing value so that the
     * same pipeline can be used in the datapath to facilitate
     * re-enabling the port.
     */
    if (*req_vportid < end_vportid && *req_vportid >= start_vportid) {
        if (vport_table->entry[*req_vportid].in_use == false) {
            if(vport_table->entry[*req_vportid].lcore_id == UINT32_MAX) {
                vport_table->entry[*req_vportid].lcore_id = *lcore_id;
            }
            else {
                /* return previously used lcore_id back to caller */
                *lcore_id = vport_table->entry[*req_vportid].lcore_id;
            }
            vport_table->entry[*req_vportid].in_use = true;
            vport_table->entry[*req_vportid].type = type;
            strncpy(vport_table->entry[*req_vportid].name, name,
                    OVDK_MAX_VPORT_NAMESIZE - 1);
            ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);
            return 0;
        /* we should only return immediately if the device is a phy port (where
         * port number matters!) */
        } else if (type == OVDK_VPORT_TYPE_PHY) {
            ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);
            return -EBUSY;
        }
    }

    /*
     * If the requested 'req_vportid' is not found or 'req_vportid' is
     * OVDK_MAX_VPORTS, we search all vports in the range associated with
     * 'type' to get a vportid. Coming into this loop ret is -1. If we find
     * a valid vportid we set it to 0. As above we check to see if the
     * lcore_id has been set previously to facilitate re-enabling ports
     * in the datapath.
     */
    for (vportid = start_vportid; vportid < end_vportid; vportid++) {
        if (vport_table->entry[vportid].in_use == false) {
            if(vport_table->entry[vportid].lcore_id == UINT32_MAX) {
                vport_table->entry[vportid].lcore_id = *lcore_id;
            }
            else {
                /* return previously used lcore_id back to caller */
                *lcore_id = vport_table->entry[vportid].lcore_id;
            }
            vport_table->entry[vportid].in_use = true;
            vport_table->entry[vportid].type = type;
            strncpy(vport_table->entry[vportid].name, name, OVDK_MAX_VPORT_NAMESIZE - 1);
            /* return valid vportid back to caller */
            *req_vportid = vportid;
            ret = 0;
            break;
        }
    }
    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return ret;
}

/*
 * Get the next vportid after 'vportid' that is currently in use.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters. If no more in-use
 * vportids are available between vportid and OVDK_MAX_VPORTS, returns
 * -ENOENT.
 */
int
dpif_dpdk_vport_table_entry_get_next_inuse(uint32_t *vportid)
{
    int ret = -ENOENT;

    if (vportid == NULL || *vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    /*
     * Search table for next in use. If we reach the end, 'vportid'
     * will be OVDK_MAX_VPORTS
     */
    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);
    for (; *vportid < OVDK_MAX_VPORTS; (*vportid)++) {
        if (vport_table->entry[*vportid].in_use == true) {
            ret = 0;
            break;
        }
    }

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return ret;
}

/*
 * Reset 'vportid' by marking it as not being in use.
 * Do not reset lcore_id as it is needed to identify
 * the original core a vport was added to.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters.
 */
int
dpif_dpdk_vport_table_entry_reset(uint32_t vportid)
{
    if (vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);
    vport_table->entry[vportid].in_use = false;
    vport_table->entry[vportid].type = OVDK_VPORT_TYPE_DISABLED;
    strncpy(vport_table->entry[vportid].name, "invalid" , OVDK_MAX_VPORT_NAMESIZE);
    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return 0;
}

/*
 * Reset the lcore_id for 'vportid' by setting it to
 * UINT32_MAX.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters.
 */
int
dpif_dpdk_vport_table_entry_reset_lcore_id(uint32_t vportid)
{
    if (vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);
    vport_table->entry[vportid].lcore_id = UINT32_MAX;
    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return 0;
}

/*
 * Get the lcore_id to which 'vportid' is assigned.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters.
 */
int
dpif_dpdk_vport_table_entry_get_lcore_id(uint32_t vportid,
                                         unsigned *lcore_id)
{
    if (vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    if (lcore_id == NULL) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);

    *lcore_id = vport_table->entry[vportid].lcore_id;

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return 0;
}

/*
 * Set 'vportid' as being 'in_use'.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters.
 */
int
dpif_dpdk_vport_table_entry_set_inuse(uint32_t vportid,
                                       bool inuse)
{
    if (vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);

    vport_table->entry[vportid].in_use = inuse;

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return 0;
}

/*
 * Check if 'vportid' is in use and returns via 'in_use'.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters.
 */
int
dpif_dpdk_vport_table_entry_get_inuse(uint32_t vportid,
                                       bool *inuse)
{
    if (vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    if (inuse == NULL) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);

    *inuse = vport_table->entry[vportid].in_use;

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return 0;
}

/*
 * Get the first vportid that is currently in use.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters. If none are in-use
 * returns -ENOENT.
 */
int
dpif_dpdk_vport_table_entry_get_first_inuse(uint32_t *vportid)
{
    int ret = -ENOENT;

    if (vportid == NULL) {
        return -EINVAL;
    }

    /*
     * Search table for next in use. If we reach the end, 'vportid'
     * will be OVDK_MAX_VPORTS
     */
    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);
    for (*vportid = 0; *vportid < OVDK_MAX_VPORTS; (*vportid)++) {
        if (vport_table->entry[*vportid].in_use == true) {
            ret = 0;
            break;
        }
    }

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return ret;
}


/*
 * Get the type of the vport 'vportid'.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters.
 */
int
dpif_dpdk_vport_table_entry_get_type(uint32_t vportid,
                                     enum ovdk_vport_type *type)
{
    if (vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    if (type == NULL) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);

    if (vport_table->entry[vportid].in_use == false) {
        return -ENOENT;
    }

    *type = vport_table->entry[vportid].type;

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return 0;
}

/*
 * Get the name of the vport 'vportid'.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters.
 */
int
dpif_dpdk_vport_table_entry_get_name(uint32_t vportid,
                                     char *name)
{
    if (vportid >= OVDK_MAX_VPORTS) {
        return -EINVAL;
    }

    if (name == NULL) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);

    if (vport_table->entry[vportid].in_use == false) {
        return -ENOENT;
    }

    strncpy(name, vport_table->entry[vportid].name, OVDK_MAX_VPORT_NAMESIZE);

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return 0;
}

/*
 * Get the 'vportid' of the vport of name 'name'.
 *
 * Returns 0 on success, or -EINVAL on invalid parameters. If no vport is found
 * that name it returns -ENOENT.
 */
int
dpif_dpdk_vport_table_entry_get_vportid(const char *name,
                                        unsigned *vportid)
{
    int ret = -ENOENT;

    if (vportid == NULL) {
        return -EINVAL;
    }

    if (name == NULL) {
        return -EINVAL;
    }

    /*
     * Search table. If we reach the end, 'vportid'
     * will be OVDK_MAX_VPORTS
     */
    ovs_mutex_lock(&dpif_dpdk_vport_table_mutex);
    for (*vportid = 0; *vportid < OVDK_MAX_VPORTS; (*vportid)++) {
        if (vport_table->entry[*vportid].in_use == true) {
            if (strncmp(name, vport_table->entry[*vportid].name,
                        OVDK_MAX_VPORT_NAMESIZE) == 0) {
                ret = 0;
                break;
            }
        }
    }

    ovs_mutex_unlock(&dpif_dpdk_vport_table_mutex);

    return ret;
}
