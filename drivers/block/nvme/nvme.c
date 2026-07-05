#include <common/spinlock.h>
#include <drivers/block/nvme.h>
#include <immintrin.h>
#include <kernel/hal/pci/pci.h>
#include <kernel/mmu/mmu.h>

static void forceReadVolatile(const void* var) {
    volatile void* tmp = *(volatile void**)&var;
    __asm__ volatile("" : "+m"(tmp));
}

#define CAP_MAXQUEUE_ENTRIES (cap & 0xFFFF)

typedef struct __attribute__((packed)) NVMeCommand {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mdPtr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cwd10;
    uint32_t cwd11;
    uint32_t cwd12;
    uint32_t cwd13;
    uint32_t cwd14;
    uint32_t cwd15;
} NVMeCommand;
typedef struct __attribute__((packed)) NVMeCompletion {
    uint32_t command;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cmd_id;
    uint8_t  phase : 1;
    uint16_t status : 15;
} NVMeCompletion;
typedef struct NVMeQueue {
    uint64_t addr;
    uint64_t size;
    uint64_t head;
    uint64_t tail;
    uint64_t qid;
    uint8_t  phase;
} NVMeQueue;
typedef struct __attribute__((packed)) NVMeLbaf {
    uint16_t metaSize;
    uint8_t  lbads;
    uint8_t  rp;
} NVMeLbaf;
typedef struct __attribute__((packed)) NVMeIdentifyNamespace {
    uint64_t namespaceSize;
    uint64_t namespaceCapacity;
    uint64_t namespaceUtilization;
    uint8_t  namespaceFeatures;
    uint8_t  nLBAF;
    uint8_t  FLBAS;
    uint8_t  metadataCap;
    uint8_t  dpc;
    uint8_t  dps;
    uint8_t  reserved1[98];
    NVMeLbaf lbafs[16];
    uint8_t  reserved2[3904];
} NVMeIdentifyNamespace;
typedef struct NVMeMSCData {
    void*      baseAddr;
    NVMeQueue* admSq;
    NVMeQueue* admCq;
    NVMeQueue* ioSq;
    NVMeQueue* ioCq;
    uint64_t   nsid;
    uint64_t   diskSize;
    Spinlock   lock;
} NVMeMSCData;

static atomic_char32_t cmdId = 1;

static int64_t intpow(int64_t base, int64_t exp) {
    int64_t result = 1;
    while (exp > 0) {
        if (exp % 2 == 1) result *= base;
        base *= base;
        exp /= 2;
    }
    return result;
}

uint32_t readReg(NVMeMSCData* this, uint32_t offset) {
    volatile uint32_t* reg = (volatile uint32_t*)((uint64_t)this->baseAddr + offset);
    if (getPhysicalAddr(vmmGetPML4(0), (uint64_t)reg, false) == 0) {
        vmmMapPage(vmmGetPML4(0), (size_t)reg, (size_t)reg,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    volatile uint32_t ret = *reg;
    forceReadVolatile((void*)(uint64_t)ret);
    // debug("r base addr: 0x%llx offset: 0x%llx reg: 0x%llx ret: 0x%llx\n", this->baseAddr, offset,
    //       reg, ret);
    return ret;
}
uint64_t readReg64(NVMeMSCData* this, uint32_t offset) {
    volatile uint64_t* reg = (volatile uint64_t*)((uint64_t)this->baseAddr + offset);
    if (getPhysicalAddr(vmmGetPML4(0), (uint64_t)reg, false) == 0) {
        vmmMapPage(vmmGetPML4(0), (size_t)reg, (size_t)reg,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    volatile uint64_t ret = *reg;
    forceReadVolatile((void*)(uint64_t)ret);
    // debug("r64 base addr: 0x%llx offset: 0x%llx reg: 0x%llx ret: 0x%llx\n", this->baseAddr,
    // offset,
    //       reg, ret);
    return ret;
}
void writeReg(NVMeMSCData* this, uint32_t offset, uint32_t val) {
    volatile uint32_t* reg = (volatile uint32_t*)((uint64_t)this->baseAddr + offset);
    if (getPhysicalAddr(vmmGetPML4(0), (uint64_t)reg, false) == 0) {
        vmmMapPage(vmmGetPML4(0), (size_t)reg, (size_t)reg,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    // debug("w base addr: 0x%llx offset: 0x%llx reg: 0x%llx value: 0x%llx\n", this->baseAddr,
    // offset,
    //       reg, val);
    *reg = val;
}
void writeReg64(NVMeMSCData* this, uint32_t offset, uint64_t val) {
    volatile uint64_t* reg = (volatile uint64_t*)((uint64_t)this->baseAddr + offset);
    if (getPhysicalAddr(vmmGetPML4(0), (uint64_t)reg, false) == 0) {
        vmmMapPage(vmmGetPML4(0), (size_t)reg, (size_t)reg,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    // debug("w64 base addr: 0x%llx offset: 0x%llx reg: 0x%llx value: 0x%llx\n", this->baseAddr,
    //       offset, reg, val);
    *reg = val;
}

static bool sendCmd(NVMeMSCData* this, volatile NVMeQueue* sq, volatile NVMeQueue* cq,
                    volatile NVMeCommand* cmd) {
    volatile uint8_t*        entry = (uint8_t*)sq->addr + (sq->tail * sizeof(NVMeCommand));
    volatile NVMeCompletion* cqe =
        (volatile NVMeCompletion*)(cq->addr + cq->head * sizeof(NVMeCompletion));
    debug("SQ addr 0x%lx\n", sq->addr);
    debug("Sending command 0x%lx CQE->cmd_id = %x sq_id = %x\n", cmd->opcode, cmd->command_id,
          sq->qid);
    memcpy((void*)entry, (void*)cmd, sizeof(NVMeCommand));
    __asm__ volatile("sfence" ::: "memory");
    // _movdir64b(entry, cmd);
    sq->tail = (sq->tail + 1) % sq->size;
    writeReg(this, 0x1000 + 8 * sq->qid, sq->tail);
    __asm__ volatile("mfence" ::: "memory");
    uint8_t  attempts = 255;
    uint16_t status   = 0;
    while (attempts) {
        uint32_t timeout = 500000;
        while (cqe->phase != cq->phase && timeout) {
            __asm__("nop" ::: "memory");
            timeout--;
            // error("Current phase is invalid\n");
        }
        // while (timeout) {
        //     __asm__("nop");
        //     timeout--;
        // }
        status = cqe->status;
        // debug("CQE->status = %015.15b CQ->PHASE = %01.1b CQE->PHASE = %01.1b CQE->cmd_id = %x "
        //       "CQE->sq_id = %x condition = %s\n",
        //       cqe->status, cq->phase, cqe->phase, cqe->cmd_id, cqe->sq_id,
        //       (cqe->status == 0 && cqe->cmd_id == cmd->command_id && cqe->sq_id == sq->qid)
        //           ? "true"
        //           : "false");
        if (cqe->phase == cq->phase && cqe->cmd_id == cmd->command_id && cqe->sq_id == sq->qid) {
            break;
        }
        attempts--;
    }
    if (attempts == 0) {
        error("Exhausted attempts\n");
    }
    if (status != 0) {
        error("Status was non zero: %032.32b\n", status);
    }
    cq->head++;
    if (cq->head == cq->size) {
        cq->head = 0;
        cq->phase ^= 1;
    }
    writeReg(this, 0x1000 + 8 * cq->qid + 4, cq->head);
    __asm__ volatile("sfence" ::: "memory");
    return status == 0;
}

static bool NvmeRead(MSCDriver* mscDriver, void* buffer, size_t offset, size_t length) {
    NVMeMSCData* this = mscDriver->driverData;
    LOCK(this->lock);
    NVMeCommand* cmd = malloc(sizeof(NVMeCommand));
    if (!cmd) {
        error("Failed to allocate enough bytes for NVMeCommand read\n");
    }
    uint64_t pageAlignBuffer = (uint64_t)pmmAllocateSize(length * 512);
    for (size_t i = 0; i < length * 512; i += PAGE_SIZE) {
        vmmMapPage(vmmGetPML4(0), (size_t)((size_t)pageAlignBuffer + i),
                   (size_t)((size_t)pageAlignBuffer + i),
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    memset(cmd, 0, sizeof(NVMeCommand));
    memset((void*)pageAlignBuffer, 0, length * 512);
    cmd->command_id = cmdId++;
    cmd->opcode     = 0x02;
    cmd->nsid       = this->nsid;
    cmd->prp1       = pageAlignBuffer;
    debug("Reading offset %lu\n", offset);
    cmd->cwd10  = (uint32_t)offset;
    cmd->cwd11  = (uint32_t)((uint64_t)offset >> 32);
    cmd->cwd12  = (uint16_t)(length - 1);
    cmd->cwd13  = 2;
    bool result = sendCmd(this, this->ioSq, this->ioCq, cmd);
    memcpy((void*)buffer, (void*)pageAlignBuffer, length * 512);
    for (size_t i = 0; i < length * 512; i += PAGE_SIZE) {
        vmmUnmapPage(vmmGetPML4(0), pageAlignBuffer + i);
    }
    pmmFree((void*)pageAlignBuffer, length * 512);
    free(cmd);
    UNLOCK(this->lock);
    return result;
}

static bool NvmeWrite(MSCDriver* mscDriver, const void* buffer, size_t offset, size_t length) {
    NVMeMSCData* this = mscDriver->driverData;
    LOCK(this->lock);
    NVMeCommand* cmd = malloc(sizeof(NVMeCommand));
    if (!cmd) {
        error("Failed to allocate enough bytes for NVMeCommand write\n");
    }
    memset(cmd, 0, sizeof(NVMeCommand));
    uint64_t pageAlignBuffer = (uint64_t)pmmAllocateSize(length * 512);
    for (size_t i = 0; i < length * 512; i += PAGE_SIZE) {
        vmmMapPage(vmmGetPML4(0), (size_t)((size_t)pageAlignBuffer + i),
                   (size_t)((size_t)pageAlignBuffer + i),
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    memcpy((void*)pageAlignBuffer, (void*)buffer, length * 512);
    for (size_t i = 0; i < length * 512; i += PAGE_SIZE) {
        vmmUnmapPage(vmmGetPML4(0), pageAlignBuffer + i);
    }
    cmd->command_id = cmdId++;
    cmd->opcode     = 0x01;
    cmd->nsid       = this->nsid;
    cmd->prp1       = pageAlignBuffer;
    cmd->cwd10      = (uint32_t)offset;
    cmd->cwd11      = (uint32_t)((uint64_t)offset >> 32);
    cmd->cwd12      = (uint16_t)(length - 1);
    cmd->cwd13      = 2;
    bool result     = sendCmd(this, this->ioSq, this->ioCq, cmd);
    pmmFree((void*)pageAlignBuffer, length * 512);
    free(cmd);
    UNLOCK(this->lock);
    return result;
}

static bool NvmeWantsPCI() {
    return true;
}

static void createAdmQueue(NVMeMSCData* this, uint64_t cap) {
    size_t submissionQueueSize = CAP_MAXQUEUE_ENTRIES * sizeof(NVMeCommand);
    size_t completionQueueSize = CAP_MAXQUEUE_ENTRIES * sizeof(NVMeCompletion);
    this->admSq                = malloc(sizeof(NVMeQueue));
    this->admCq                = malloc(sizeof(NVMeQueue));
    this->admSq->addr          = (uint64_t)pmmAllocateSize(submissionQueueSize);
    this->admCq->addr          = (uint64_t)pmmAllocateSize(completionQueueSize);
    this->admSq->size          = CAP_MAXQUEUE_ENTRIES;
    this->admCq->size          = CAP_MAXQUEUE_ENTRIES;
    for (size_t i = 0; i < completionQueueSize; i += PAGE_SIZE) {
        vmmMapPage(vmmGetPML4(0), this->admCq->addr + i, this->admCq->addr + i,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    for (size_t i = 0; i < submissionQueueSize; i += PAGE_SIZE) {
        vmmMapPage(vmmGetPML4(0), this->admSq->addr + i, this->admSq->addr + i,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    memset((void*)this->admSq->addr, 0, submissionQueueSize);
    memset((void*)this->admCq->addr, 0, completionQueueSize);
    this->admCq->qid   = 0;
    this->admSq->qid   = 0;
    this->admCq->head  = 0;
    this->admSq->tail  = 0;
    this->admCq->phase = 1;
    this->admSq->phase = 1;
    writeReg64(this, 0x28, this->admSq->addr);
    writeReg64(this, 0x30, this->admCq->addr);
}

static void registerIoQueues(NVMeMSCData* this) {
    NVMeCommand* createIOQueue = malloc(sizeof(NVMeCommand));
    if (!createIOQueue) {
        error("Failed to allocate NVMeCommand\n");
    }
    memset(createIOQueue, 0, sizeof(NVMeCommand));
    createIOQueue->prp1       = this->ioCq->addr;
    createIOQueue->cwd10      = ((uint32_t)this->ioCq->qid) | ((uint32_t)this->ioCq->size << 16);
    createIOQueue->cwd11      = 1;
    createIOQueue->opcode     = 5;
    createIOQueue->command_id = cmdId++;
    sendCmd(this, this->admSq, this->admCq, createIOQueue);
    this->ioCq->phase = 1;
    memset(createIOQueue, 0, sizeof(NVMeCommand));
    createIOQueue->prp1       = this->ioSq->addr;
    createIOQueue->cwd10      = ((uint32_t)this->ioSq->qid) | ((uint32_t)this->ioSq->size << 16);
    createIOQueue->cwd11      = ((uint32_t)this->ioCq->qid << 16) | 1;
    createIOQueue->opcode     = 1;
    createIOQueue->command_id = cmdId++;
    sendCmd(this, this->admSq, this->admCq, createIOQueue);
    this->ioSq->phase = 1;
    free(createIOQueue);
}

static void createIoQueues(NVMeMSCData* this, uint64_t cap) {
    size_t submissionQueueSize = CAP_MAXQUEUE_ENTRIES * sizeof(NVMeCommand);
    size_t completionQueueSize = CAP_MAXQUEUE_ENTRIES * sizeof(NVMeCompletion);
    this->ioCq                 = malloc(sizeof(NVMeQueue));
    this->ioSq                 = malloc(sizeof(NVMeQueue));
    this->ioSq->addr           = (uint64_t)pmmAllocateSize(submissionQueueSize);
    this->ioCq->addr           = (uint64_t)pmmAllocateSize(completionQueueSize);
    for (size_t i = 0; i < completionQueueSize; i += PAGE_SIZE) {
        vmmMapPage(vmmGetPML4(0), this->ioCq->addr + i, this->ioCq->addr + i,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    for (size_t i = 0; i < submissionQueueSize; i += PAGE_SIZE) {
        vmmMapPage(vmmGetPML4(0), this->ioSq->addr + i, this->ioSq->addr + i,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
    memset((void*)this->ioSq->addr, 0, submissionQueueSize);
    memset((void*)this->ioCq->addr, 0, completionQueueSize);
    this->ioSq->size  = CAP_MAXQUEUE_ENTRIES;
    this->ioCq->size  = CAP_MAXQUEUE_ENTRIES;
    this->ioCq->head  = 0;
    this->ioSq->tail  = 0;
    this->ioCq->qid   = 1;
    this->ioSq->qid   = 1;
    this->ioCq->phase = 0;
    this->ioSq->phase = 0;
    registerIoQueues(this);
}

static void identifyController(NVMeMSCData* this) {
    uint8_t* addr = (uint8_t*)pmmAllocate();
    vmmMapPage(vmmGetPML4(0), (size_t)addr, (size_t)addr,
               MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
               MAP_UC | MAP_PRESENT);
    memset(addr, 0, PAGE_SIZE);
    NVMeCommand* identifyCommand = malloc(sizeof(NVMeCommand));
    if (!identifyCommand) {
        error("Failed to allocate identify command\n");
    }
    memset(identifyCommand, 0, sizeof(NVMeCommand));
    identifyCommand->prp1       = (uint64_t)addr;
    identifyCommand->command_id = cmdId++;
    identifyCommand->opcode     = 0x06;
    identifyCommand->cwd10      = 0x02;
    sendCmd(this, this->admSq, this->admCq, identifyCommand);
    this->nsid = 0;
    for (size_t i = 0; i < PAGE_SIZE; i += 4) {
        uint32_t curNsid = *(uint32_t*)(addr + i);
        if (curNsid == 0) {
            break;
        }
        if (this->nsid != 0) {
            todo(true, "Support multiple NSIDs\n");
        }
        this->nsid = curNsid;
    }
    debug("NSID = 0x%lx\n", this->nsid);
    memset(identifyCommand, 0, sizeof(NVMeCommand));
    identifyCommand->prp1       = (uint64_t)addr;
    identifyCommand->command_id = cmdId++;
    identifyCommand->opcode     = 0x06;
    identifyCommand->cwd10      = 0x00;
    identifyCommand->nsid       = this->nsid;
    sendCmd(this, this->admSq, this->admCq, identifyCommand);
    NVMeIdentifyNamespace identifyNamespace;
    memcpy(&identifyNamespace, addr, sizeof(NVMeIdentifyNamespace));
    if (identifyNamespace.lbafs[identifyNamespace.FLBAS].lbads != 9) {
        todo(true, "Support non 512 blocks\n");
    }
    this->diskSize = identifyNamespace.namespaceSize;
    free(identifyCommand);
}

static void NvmeInit(MSCDriver* mscDriver, PCIDevice* pciDev) {
    NVMeMSCData* this = mscDriver->driverData;
    LOCK(this->lock);
    pciEnableBusmaster(pciDev);
    uint32_t BAR0  = pciReadConfig(pciDev, 0x10);
    uint32_t BAR1  = pciReadConfig(pciDev, 0x14);
    this->baseAddr = (void*)(((uint64_t)BAR1 << 32) | (BAR0 & 0xFFFFFFF0));
    vmmMapPage(vmmGetPML4(0), (uint64_t)this->baseAddr, (uint64_t)this->baseAddr,
               MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
               MAP_GLOBAL | MAP_PRESENT | MAP_UC | MAP_WT);
    uint64_t cap = readReg64(this, 0x00);
    // std::memcpy(&this->capabilities, &cap, sizeof(cap));
    // if (intpow(2, 12 + this->capabilities.MinPagesize) != PAGE_SIZE) {
    //     dbg::printm(MODULE, "Page sizes of 0x%llx not supported, minimum size: 0x%llx\n",
    //     PAGE_SIZE,
    //                 intpow(2, 12 + this->capabilities.MinPagesize));
    //     std::abort();
    // }
    // if (this->capabilities.DefaultCommandSet == 0) {
    //     dbg::printm(MODULE, "Default command set not supported\n");
    //     std::abort();
    // }
    // if (this->capabilities.CSS_AdminOnly == 1) {
    //     dbg::printm(MODULE, "Admin only commands are not supported\n");
    //     std::abort();
    // }
    // if (this->capabilities.CSS_MultipleIo == 0) {
    //     dbg::printm(MODULE, "I/O commands are not supported\n");
    //     std::abort();
    // }
    // if (this->capabilities.DoorbellStride != 0) {
    //     dbg::printm(MODULE, "TODO: Support DoorbellStride\n");
    //     std::abort();
    // }
    uint32_t cc = readReg(this, 0x14);
    cc &= (uint32_t)~1;
    writeReg(this, 0x14, cc);
    while ((readReg(this, 0x14) & 0x1) != 0) {
        __asm__("nop");
    }
    uint32_t csts = readReg(this, 0x1C);
    if ((csts & 0x1) != 0) {
        error("Failed to disable controller\n");
    }
    createAdmQueue(this, cap);
    writeReg(this, 0x24, ((this->admCq->size - 1) << 16) | (this->admSq->size - 1));
    cc                  = readReg(this, 0x14);
    uint32_t iosqes_val = 6;
    uint32_t iocqes_val = 4;
    cc |= (iosqes_val & 0x1F) << 16;
    cc |= (iocqes_val & 0x0F) << 20;
    cc |= 1;
    writeReg(this, 0x14, cc);
    while ((readReg(this, 0x1C) & 0x1) == 0) {
        __asm__("nop");
    }
    csts = readReg(this, 0x1C);
    if ((csts & ~1) != 0) {
        error("Controller status error bits are set: 0b%032b\n", csts);
    }
    createIoQueues(this, cap);
    identifyController(this);
    UNLOCK(this->lock);
}

static void NvmeDeinit(MSCDriver* mscDriver) {
    free(mscDriver->driverData);
}

MSCDriver* loadNVMeDriver() {
    MSCDriver* ret = malloc(sizeof(MSCDriver));
    if (!ret) {
        error("Failed to allocate memory for RAM MSC\n");
    }
    ret->deinit          = NvmeDeinit;
    ret->read            = NvmeRead;
    ret->write           = NvmeWrite;
    ret->wantsPCI        = NvmeWantsPCI;
    ret->init            = NvmeInit;
    ret->issueCommand    = NULL;
    NVMeMSCData* drvData = malloc(sizeof(NVMeMSCData));
    if (!drvData) {
        error("Failed to allocate memory for Nvme driver data\n");
    }
    memset(drvData, 0, sizeof(NVMeMSCData));
    ret->driverData = (void*)drvData;
    return ret;
}