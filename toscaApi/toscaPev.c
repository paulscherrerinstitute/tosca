#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/select.h>

#include <pevulib.h>
#include <pevxulib.h>

#include "toscaMap.h"
#include "toscaReg.h"
#include "toscaIntr.h"
#include "toscaDma.h"
#include "toscaPev.h"
#include "i2c.h"

#define TOSCA_DEBUG_NAME pev
#include "toscaDebug.h"

/** INIT **************************************************/

static uint defaultCrate;

struct pevx_node* pevx_init(uint crate)
{
    debug("pevx compatibility mode");
    return (void*) -1;
}

struct pev_node* pev_init(uint crate)
{
    debug("pev compatibility mode");
    defaultCrate = crate;
    return (void*) -1;
}

/** CSR ***************************************************/

int pevx_csr_rd(uint crate, int address)
{
    if (address & 0x80000000)
        return toscaCsrRead((crate << 16) | (address & 0x7FFFFFFF));
    else
        return toscaIoRead((crate << 16) | address);
}

int pev_csr_rd(int address)
{
    return pevx_csr_rd(defaultCrate, address);
}

int pevx_csr_wr(uint crate, int address, int value)
{
    if (address & 0x80000000)
        return toscaCsrWrite((crate << 16) | (address & 0x7FFFFFFF), value);
    else
        return toscaIoWrite((crate << 16) | address, value);
}

void pev_csr_wr(int address, int value)
{
    pevx_csr_wr(defaultCrate, address, value);
}

int pevx_csr_set(uint crate, int address, int value)
{
    if (address & 0x80000000)
        return toscaCsrSet((crate << 16) | (address & 0x7FFFFFFF), value);
    else
        return toscaIoSet((crate << 16) | address, value);
}

void pev_csr_set(int address, int value)
{
    pevx_csr_set(defaultCrate, address, value);
}

/** ELB AND SRAM ******************************************/

static uint32_t *sramPtr(size_t address)
{
    static volatile void* sram = NULL;
    if (address > 0x2000)
    {
        errno = EFAULT;
        return NULL;
    }
    if (!sram)
    {
        sram = toscaMap(TOSCA_SRAM, 0, 0, 0);
        if (!sram) return NULL;
    }
    return (uint32_t*) ((size_t) sram + address);
}

int pevx_elb_rd(uint crate, int address)
{
    if (address >= 0xe000) /* sram */
    {
        uint32_t* ptr = sramPtr(address - 0xe000);
        if (!ptr) return -1;
        return *ptr;
    }
    else
    {
        return toscaPonRead(address);
    }
}

int pev_elb_rd(int address)
{
    return pevx_elb_rd(defaultCrate, address);
}

int pevx_elb_wr(uint crate, int address, int value)
{
    if (address >= 0xe000) /* sram */
    {
        uint32_t* ptr = sramPtr(address - 0xe000);
        if (!ptr) return -1;
        *ptr = value;
        return 0;
    }
    else
    {
        return toscaPonWrite(address, value);
    }
}

int pev_elb_wr(int address, int value)
{
    return pevx_elb_wr(defaultCrate, address, value);
}

/** SMON **************************************************/

int pev_smon_rd(int address)
{
    return toscaSmonRead(address);
}

void pev_smon_wr(int address, int value)
{
    toscaSmonWrite(address, value);
}

/** BMR ***************************************************/

static int pev_bmr_fd(uint bmr, uint address)
{
    static int fd[4] = {0};

    if (bmr > 3)
    {
        errno = EINVAL;
        return -1;
    }
    if (!fd[bmr])
        fd[bmr] = i2cOpen("/sys/devices/*localbus/*000a0.pon-i2c/i2c-*", bmr == 3 ? 0x24 : 0x53 + bmr * 8);
    if (fd[bmr] < 0)
        errno = ENODEV;
    return fd[bmr];
}

int pev_bmr_read(uint bmr, uint address, uint *value, uint count)
{
    int fd;
    debug("bmr=%u address=0x%x, count=%u", bmr, address, count);
    fd = pev_bmr_fd(bmr, address);
    if (fd < 0) return -1;
    return i2cRead(fd, address, count, value);
}

int pev_bmr_write(uint bmr, uint address, uint value, uint count)
{
    int fd;
    debug("bmr=%u address=0x%x, value=0x%x  count=%u", bmr, address, value, count);
    fd = pev_bmr_fd(bmr, address);
    if (fd < 0) return -1;
    return i2cWrite(fd, address, count, value);
}

float pev_bmr_conv_11bit_u(ushort value)
{
    ushort l;
    short h;

    l = value & 0x7ff;
    h = value >> 11;
    h |= 0xffe0;
    h = ~h + 1;
    return(((float)l/(1 << h)));
}

float pev_bmr_conv_11bit_s(ushort value)
{
    short h,l;

    l = value & 0x7ff;
    if( l & 0x400) l |= 0xf800;
    h = value >> 11;
    h |= 0xffe0;
    h = ~h + 1;
    return(((float)l/(1 << h)));
}

float pev_bmr_conv_16bit_u(ushort value)
{
    return(((float)value/(1 << 13)));
}

/** MAPS **************************************************/

static int pev_mode_to_tosca_addrspace(int mode)
{
    switch (mode & 0xff00)
    {
        case MAP_SPACE_VME|MAP_VME_CR:  return VME_CRCSR;
        case MAP_SPACE_VME|MAP_VME_A16: return VME_A16;
        case MAP_SPACE_VME|MAP_VME_A24: return VME_A24;
        case MAP_SPACE_VME|MAP_VME_A32: return VME_A32;
        case MAP_SPACE_SHM:             return TOSCA_SMEM;
        case MAP_SPACE_USR1:            return TOSCA_USER1;
        case MAP_SPACE_USR2:            return TOSCA_USER2;
        case MAP_SPACE_PCIE:            return VME_SLAVE;
        default: return 0;
    }
}

int pevx_map_alloc(uint crate, struct pev_ioctl_map_pg *map_p)
{
    volatile void* ptr;
    toscaMapInfo_t mapInfo;
    
    map_p->offset = 0;
    map_p->win_size = 0;
    map_p->rem_base = 0;
    map_p->loc_base = 0;
    map_p->pci_base = 0;

    if (map_p->sg_id == MAP_SLAVE_VME && (map_p->mode & 0xf000) != MAP_SPACE_PCIE)
    {
        /* for slave maps, loc_addr is the vme address and rem_add is the resource address */

        debug("VME slave sg_id=0x%x mode=0x%x size=0x%x rem_addr=0x%lx loc_addr=0x%lx",
            map_p->sg_id, map_p->mode, map_p->size, map_p->rem_addr, map_p->loc_addr);

        toscaMap(crate<<16 | pev_mode_to_tosca_addrspace(map_p->mode) | MAP_SLAVE_VME |
            (map_p->mode & MAP_SWAP_AUTO) ? VME_SWAP : 0,
            map_p->loc_addr, map_p->size, map_p->rem_addr);

        map_p->win_size = (map_p->size + (map_p->loc_addr & 0xfffffu) + 0xfffffu) & ~0xfffffu;
        map_p->rem_base = map_p->rem_addr - (map_p->loc_addr & 0xfffffu);
        map_p->loc_base = map_p->loc_addr & ~0xfffffu;
        return 0;
    }

    debug("sg_id=0x%x mode=0x%x size=0x%x rem_addr=0x%lx",
        map_p->sg_id, map_p->mode, map_p->size, map_p->rem_addr);

    if (map_p->flag & MAP_FLAG_FORCE)
    {
        error("MAP_FLAG_FORCE not supported");
        return -1;
    }

    map_p->loc_addr = 0;

    ptr = toscaMap(crate<<16 | pev_mode_to_tosca_addrspace(map_p->mode),
        map_p->rem_addr, map_p->size, 0);

    if (!ptr) return -1;
    mapInfo = toscaMapFind(ptr);
    map_p->win_size = mapInfo.size;
    map_p->rem_base = mapInfo.baseaddress;
    map_p->loc_base = (size_t) mapInfo.baseptr;
    map_p->usr_addr = (void*) ptr;
    return 0;
}

int pev_map_alloc(struct pev_ioctl_map_pg *map_p)
{
    return pevx_map_alloc(defaultCrate, map_p);
}

void *pevx_mmap(uint crate, struct pev_ioctl_map_pg *map_p)
{
    return map_p->usr_addr;
}

void *pev_mmap(struct pev_ioctl_map_pg *map_p)
{
    return map_p->usr_addr;
}

int pevx_munmap(uint crate, struct pev_ioctl_map_pg *map_p)
{
    /* do nothing, tosca will clean up at exit */
    return 0;
}

int pev_munmap(struct pev_ioctl_map_pg *map_p)
{
    /* do nothing, tosca will clean up at exit */
    return 0;
}

int pevx_map_free(uint crate, struct pev_ioctl_map_pg *map_p)
{
    /* do nothing, tosca will clean up at exit */
    return 0;
}

int pev_map_free(struct pev_ioctl_map_pg *map_p)
{
    /* do nothing, tosca will clean up at exit */
    return 0;
}

int pevx_map_modify(uint crate, struct pev_ioctl_map_pg *map_p)
{
    /* do nothing, cannot modify */
    return -1;
}

int pev_map_modify(struct pev_ioctl_map_pg *map_p)
{
    /* do nothing, cannot modify */
    return -1;
}

/** INTR **************************************************/

typedef struct {
    int fd[2];
    int enabled;
    intrmask_t mask;
} my_pev_evt_queue;

struct pev_ioctl_evt *pevx_evt_queue_alloc(uint crate, int sig)
{
    struct pev_ioctl_evt *evt;
    if (crate > 0) return NULL;
    evt = calloc(1, sizeof(struct pev_ioctl_evt) + sizeof(my_pev_evt_queue));
    evt->sig = sig;
    evt->evt_queue = evt + 1;
    pipe(((my_pev_evt_queue*)evt->evt_queue)->fd);
    return evt;
}

struct pev_ioctl_evt *pev_evt_queue_alloc(int sig)
{
    return pevx_evt_queue_alloc(defaultCrate, sig);
}

static void pev_intr_vme(struct pev_ioctl_evt *evt, int inum, int vec)
{
    my_pev_evt_queue *q = (my_pev_evt_queue*)evt->evt_queue;
    debug("inum=%i vec=%i enabled=%i", inum, vec, q->enabled);
    if (!q->enabled) return;
    uint16_t ev = (EVT_SRC_VME | inum) << 8 | vec;
    debug("ev=0x%04x sig=%i", ev, evt->sig);
    write(q->fd[1], &ev, 2);
    if (evt->sig) kill(getpid(), evt->sig);
}

static void pev_intr_usr(struct pev_ioctl_evt *evt, int inum)
{
    my_pev_evt_queue *q = (my_pev_evt_queue*)evt->evt_queue;
    debug("inum=%i enabled=%i", inum, q->enabled);
    if (!q->enabled) return;
    uint16_t ev = (EVT_SRC_USR1 | inum) << 8;
    debug("ev=0x%04x sig=%i", ev, evt->sig);
    write(q->fd[1], &ev, 2);
    if (evt->sig) kill(getpid(), evt->sig);
}

int pevx_evt_register(uint crate, struct pev_ioctl_evt *evt, int src_id)
{
    my_pev_evt_queue *q = (my_pev_evt_queue*)evt->evt_queue;
    intrmask_t mask;
    switch (src_id & 0xf0)
    {
        case EVT_SRC_VME:
            mask = TOSCA_VME_INTR_VECS(src_id & 0xf, 0, 255);
            if (q->mask & mask & TOSCA_VME_INTR_ANY) return -1;
            toscaIntrConnectHandler(mask, pev_intr_vme, evt);
            break;
        case EVT_SRC_USR1:
        case EVT_SRC_USR2:
            mask = TOSCA_USER1_INTR(src_id & 0x1f);
            if (q->mask & mask) return -1;
            toscaIntrConnectHandler(mask, pev_intr_usr, evt);
            break;
        default: return -1;
    }
    q->mask |= mask;
    return 0;
}

int pev_evt_register(struct pev_ioctl_evt *evt, int src_id)
{
    return pevx_evt_register(defaultCrate, evt, src_id);
}

int pevx_evt_queue_free(uint crate, struct pev_ioctl_evt *evt)
{
    my_pev_evt_queue *q = (my_pev_evt_queue*)evt->evt_queue;
    q->enabled = 0;
    toscaIntrDisconnectHandler(TOSCA_VME_INTR_ANY_VECS(0, 255), pev_intr_vme, evt);
    toscaIntrDisconnectHandler(TOSCA_USER_INTR_ANY, pev_intr_usr, evt);
    close(q->fd[0]);
    close(q->fd[1]);
    free(evt);
    return 0;
}

int pev_evt_queue_free(struct pev_ioctl_evt *evt)
{
    return pevx_evt_queue_free(defaultCrate, evt);
}

int pevx_evt_read(uint crate, struct pev_ioctl_evt *evt, int timeout)
{
    struct timeval tv;
    fd_set read_fs;
    int fd = ((my_pev_evt_queue*)evt->evt_queue)->fd[0];
    int n;
    uint16_t ev;

    FD_ZERO(&read_fs);
    FD_SET(fd, &read_fs);
    if (timeout >= 0)
    {
        tv.tv_sec = timeout/1000;
        tv.tv_usec = (timeout%1000)*1000;
    }
    n = select(fd + 1, &read_fs, NULL, NULL, timeout >= 0 ? &tv : NULL);
    if (n < 1)
    {
        debugErrno("crate=%d select", crate);
        return -1;
    }
    if (read(fd, &ev, 2) < 0) return -1;
    debug("crate=%d ev=0x%04x", crate, ev);
    return ev;
}

int pev_evt_read(struct pev_ioctl_evt *evt, int timeout)
{
    return pevx_evt_read(defaultCrate, evt, timeout);
}

int pevx_evt_queue_enable(uint crate, struct pev_ioctl_evt *evt)
{
    my_pev_evt_queue *q = (my_pev_evt_queue*)evt->evt_queue;
    q->enabled = 1;
    return 0;
}

int pev_evt_queue_enable(struct pev_ioctl_evt *evt)
{
    return pevx_evt_queue_enable(defaultCrate, evt);
}

int pevx_evt_queue_disable(uint crate, struct pev_ioctl_evt *evt)
{
    my_pev_evt_queue *q = (my_pev_evt_queue*)evt->evt_queue;
    q->enabled = 0;
    return 0;
}

int pev_evt_queue_disable(struct pev_ioctl_evt *evt)
{
    return pevx_evt_queue_disable(defaultCrate, evt);
}

int pevx_evt_mask(uint crate, struct pev_ioctl_evt *evt, int src_id)
{
    intrmask_t mask;
    switch (src_id & 0xf0)
    {
        case EVT_SRC_VME:
            mask = TOSCA_VME_INTR_VECS(src_id & 0xf, 0, 255);
            break;
        case EVT_SRC_USR1:
        case EVT_SRC_USR2:
            mask = TOSCA_USER1_INTR(src_id & 0x1f);
            break;
        default: return -1;
    }
    toscaIntrDisable(mask);
    return 0;
}

int pev_evt_mask(struct pev_ioctl_evt *evt, int src_id)
{
    return pevx_evt_mask(defaultCrate, evt, src_id);
}

int pevx_evt_unmask(uint crate, struct pev_ioctl_evt *evt, int src_id)
{
    intrmask_t mask;
    switch (src_id & 0xf0)
    {
        case EVT_SRC_VME:
            mask = TOSCA_VME_INTR_VECS(src_id & 0xf, 0, 255);
            break;
        case EVT_SRC_USR1:
        case EVT_SRC_USR2:
            mask = TOSCA_USER1_INTR(src_id & 0x1f);
            break;
        default: return -1;
    }
    toscaIntrEnable(mask);
    return 0;
}

int pev_evt_unmask(struct pev_ioctl_evt *evt, int src_id)
{
    return pevx_evt_unmask(defaultCrate, evt, src_id);
}

/** DMA ***************************************************/

void *pevx_buf_alloc(uint crate, struct pev_ioctl_buf *buf)
{
    buf->u_addr = valloc(buf->size);
    if (buf->u_addr)
    {
        int i;
        buf->k_addr = buf->u_addr;
        buf->b_addr = buf->u_addr;
        for (i = 0; i < buf->size-3; i += 4)
            ((uint32_t *) buf->u_addr)[i] = 0xdeadface;
    }
    return buf->u_addr;
}

void *pev_buf_alloc(struct pev_ioctl_buf *buf)
{
    return pevx_buf_alloc(defaultCrate, buf);
}

int pevx_buf_free(uint crate, struct pev_ioctl_buf *buf)
{
    free(buf->u_addr);
    return 0;
}

int pev_buf_free(struct pev_ioctl_buf *buf)
{
    return pevx_buf_free(defaultCrate, buf);
}

static int pev_dmaspace_to_tosca_addrspace(int dmaspace)
{
    switch (dmaspace)
    {
        case DMA_SPACE_PCIE:               return 0;
        case DMA_SPACE_USR1:
        case DMA_SPACE_USR1|DMA_SPACE_WS:
        case DMA_SPACE_USR1|DMA_SPACE_DS:
        case DMA_SPACE_USR1|DMA_SPACE_QS:  return TOSCA_USER1;
        case DMA_SPACE_SHM:
        case DMA_SPACE_SHM|DMA_SPACE_WS:
        case DMA_SPACE_SHM|DMA_SPACE_DS:
        case DMA_SPACE_SHM|DMA_SPACE_QS:   return TOSCA_SMEM;
        case DMA_SPACE_VME|DMA_VME_A32:    return VME_SCT;
        case DMA_SPACE_VME|DMA_VME_BLT:    return VME_BLT;
        case DMA_SPACE_VME|DMA_VME_MBLT:   return VME_MBLT;
        case DMA_SPACE_VME|DMA_VME_2eVME:  return VME_2eVME;
        case DMA_SPACE_VME|DMA_VME_2eFAST: return VME_2eVMEFast;
        case DMA_SPACE_VME|DMA_VME_2e160:  return VME_2eSST160;
        case DMA_SPACE_VME|DMA_VME_2e233:  return VME_2eSST267;
        case DMA_SPACE_VME|DMA_VME_2e320:  return VME_2eSST320;
        default: return -1;
    }
}

int pevx_dma_move(uint crate, struct pev_ioctl_dma_req *req)
{
    int source, dest, swap=0, timeout=-1, status;
    
    source = pev_dmaspace_to_tosca_addrspace(req->src_space);
    dest = pev_dmaspace_to_tosca_addrspace(req->des_space);
    if (source == -1 || dest == -1) return -1;
    source |= crate << 16;
    if (((req->src_space & DMA_SPACE_MASK) == DMA_SPACE_USR1 ||
        (req->src_space & DMA_SPACE_MASK) == DMA_SPACE_SHM) && req->src_space & 0x30)
        swap = 1 << (req->src_space >> 4 & 0x3);
    if (((req->des_space & DMA_SPACE_MASK) == DMA_SPACE_USR1 ||
        (req->des_space & DMA_SPACE_MASK) == DMA_SPACE_SHM) && req->des_space & 0x30)
        swap = 1 << (req->des_space >> 4 & 0x3);
    if (req->wait_mode)
        timeout = (req->wait_mode >> 4) * (int[]){0,1,10,100,1000,10000,100000,0}[req->wait_mode >> 1 & 7];
    req->dma_status = DMA_STATUS_WAITING;
    status = toscaDmaTransfer(source, req->src_addr, dest, req->des_addr, req->size, swap, timeout, NULL, NULL);
    req->dma_status = DMA_STATUS_DONE | DMA_STATUS_ENDED;
    if (status != 0)
        req->dma_status |= DMA_STATUS_ERR;
    if (status == ETIMEDOUT)
        req->dma_status |= DMA_STATUS_TMO;
    return status ? -1 : 0;
}

int pev_dma_move(struct pev_ioctl_dma_req *req)
{
    return pevx_dma_move(defaultCrate, req);
}

int pevx_dma_status(uint crate, int channel, struct pev_ioctl_dma_sts *stat)
{
    memset(stat, 0, sizeof(struct pev_ioctl_dma_sts));
    return 0;
}

int pev_dma_status(int channel, struct pev_ioctl_dma_sts *stat)
{
    return pevx_dma_status(defaultCrate, channel, stat);
}

/** Old wrappers ******************************************/

intrmask_t pev_src_id_to_mask(unsigned int src_id)
{
    switch (src_id & 0xf0)
    {
        case EVT_SRC_VME:
            return TOSCA_VME_INTR_VECS(src_id & 0xf, 0, 255);
            break;
        case EVT_SRC_USR1:
        case EVT_SRC_USR2:
            return TOSCA_USER1_INTR(src_id & 0x1f);
            break;
        default: return 0;
    }
}

volatile void* pevMapExt(unsigned int card, unsigned int sg_id, unsigned int map_mode,
    size_t logicalAddress, size_t size, unsigned int flags, size_t localAddress)
{
    unsigned int addrspace;
    
    addrspace = pev_mode_to_tosca_addrspace(map_mode);
    if (sg_id == MAP_SLAVE_VME) addrspace |= VME_SLAVE;
    if (!addrspace)
    {
        errno = EINVAL;
        return NULL;
    }
    addrspace |= (card << 16);
    return toscaMap(addrspace, logicalAddress, size, localAddress);                
}

void pevUnmap(volatile void* ptr)
{
}

int pevIntrConnect(unsigned int card, unsigned int src_id, unsigned int vec_id, void (*func)(), void* usr)
{
    if (card != 0) return -1;
    return toscaIntrConnectHandler(pev_src_id_to_mask(src_id), func, usr);
}

int pevIntrDisconnect(unsigned int card, unsigned int src_id, unsigned int vec_id, void (*func)(), void* usr)
{
    if (card != 0) return -1;
    return toscaIntrDisconnectHandler(pev_src_id_to_mask(src_id), func, usr);
}

int pevIntrEnable(unsigned int card, unsigned int src_id)
{
    if (card != 0) return -1;
    return toscaIntrEnable(pev_src_id_to_mask(src_id));
}

int pevIntrDisable(unsigned int card, unsigned int src_id)
{
    if (card != 0) return -1;
    return toscaIntrDisable(pev_src_id_to_mask(src_id));
}

int pevDmaTransfer(unsigned int card, unsigned int src_space, size_t src_addr, unsigned int des_space, size_t des_addr, size_t size, unsigned int dont_use,
    unsigned int priority, pevDmaCallback callback, void *usr)
{
    int source, dest, swap=0;
    source = pev_dmaspace_to_tosca_addrspace(src_space);
    dest = pev_dmaspace_to_tosca_addrspace(des_space);
    if (source == -1 || dest == -1) return -1;
    source |= card << 16;
    if (((src_space & DMA_SPACE_MASK) == DMA_SPACE_USR1 ||
        (src_space & DMA_SPACE_MASK) == DMA_SPACE_SHM) && src_space & 0x30)
        swap = 1 << (src_space >> 4 & 0x3);
    if (((des_space & DMA_SPACE_MASK) == DMA_SPACE_USR1 ||
        (des_space & DMA_SPACE_MASK) == DMA_SPACE_SHM) && des_space & 0x30)
        swap = 1 << (des_space >> 4 & 0x3);

    return toscaDmaTransfer(source, src_addr, dest, des_addr, size, swap, -1, callback, usr);  
}
