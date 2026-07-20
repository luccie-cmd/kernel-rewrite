#include <common/dbg/dbg.h>
#include <common/spinlock.h>
#include <drivers/block/nvme.h>
#include <kernel/hal/pci/pci.h>
#include <kernel/mmu/mmu.h>

static void forceReadVolatile(const void* var) {
    volatile void* tmp = *(volatile void**)&var;
    __asm__ volatile("" : "+m"(tmp));
}

typedef enum nvme_status_field {
    NVME_SCT_GENERIC                      = 0x0,
    NVME_SC_SUCCESS                       = 0x0,
    NVME_SC_INVALID_OPCODE                = 0x1,
    NVME_SC_INVALID_FIELD                 = 0x2,
    NVME_SC_CMDID_CONFLICT                = 0x3,
    NVME_SC_DATA_XFER_ERROR               = 0x4,
    NVME_SC_POWER_LOSS                    = 0x5,
    NVME_SC_INTERNAL                      = 0x6,
    NVME_SC_ABORT_REQ                     = 0x7,
    NVME_SC_ABORT_QUEUE                   = 0x8,
    NVME_SC_FUSED_FAIL                    = 0x9,
    NVME_SC_FUSED_MISSING                 = 0xa,
    NVME_SC_INVALID_NS                    = 0xb,
    NVME_SC_CMD_SEQ_ERROR                 = 0xc,
    NVME_SC_SGL_INVALID_LAST              = 0xd,
    NVME_SC_SGL_INVALID_COUNT             = 0xe,
    NVME_SC_SGL_INVALID_DATA              = 0xf,
    NVME_SC_SGL_INVALID_METADATA          = 0x10,
    NVME_SC_SGL_INVALID_TYPE              = 0x11,
    NVME_SC_CMB_INVALID_USE               = 0x12,
    NVME_SC_PRP_INVALID_OFFSET            = 0x13,
    NVME_SC_ATOMIC_WU_EXCEEDED            = 0x14,
    NVME_SC_OP_DENIED                     = 0x15,
    NVME_SC_SGL_INVALID_OFFSET            = 0x16,
    NVME_SC_RESERVED                      = 0x17,
    NVME_SC_HOST_ID_INCONSIST             = 0x18,
    NVME_SC_KA_TIMEOUT_EXPIRED            = 0x19,
    NVME_SC_KA_TIMEOUT_INVALID            = 0x1A,
    NVME_SC_ABORTED_PREEMPT_ABORT         = 0x1B,
    NVME_SC_SANITIZE_FAILED               = 0x1C,
    NVME_SC_SANITIZE_IN_PROGRESS          = 0x1D,
    NVME_SC_SGL_INVALID_GRANULARITY       = 0x1E,
    NVME_SC_CMD_NOT_SUP_CMB_QUEUE         = 0x1F,
    NVME_SC_NS_WRITE_PROTECTED            = 0x20,
    NVME_SC_CMD_INTERRUPTED               = 0x21,
    NVME_SC_TRANSIENT_TR_ERR              = 0x22,
    NVME_SC_ADMIN_COMMAND_MEDIA_NOT_READY = 0x24,
    NVME_SC_INVALID_IO_CMD_SET            = 0x2C,

    NVME_SC_LBA_RANGE            = 0x80,
    NVME_SC_CAP_EXCEEDED         = 0x81,
    NVME_SC_NS_NOT_READY         = 0x82,
    NVME_SC_RESERVATION_CONFLICT = 0x83,
    NVME_SC_FORMAT_IN_PROGRESS   = 0x84,

    /*
     * Command Specific Status:
     */
    NVME_SCT_COMMAND_SPECIFIC      = 0x100,
    NVME_SC_CQ_INVALID             = 0x100,
    NVME_SC_QID_INVALID            = 0x101,
    NVME_SC_QUEUE_SIZE             = 0x102,
    NVME_SC_ABORT_LIMIT            = 0x103,
    NVME_SC_ABORT_MISSING          = 0x104,
    NVME_SC_ASYNC_LIMIT            = 0x105,
    NVME_SC_FIRMWARE_SLOT          = 0x106,
    NVME_SC_FIRMWARE_IMAGE         = 0x107,
    NVME_SC_INVALID_VECTOR         = 0x108,
    NVME_SC_INVALID_LOG_PAGE       = 0x109,
    NVME_SC_INVALID_FORMAT         = 0x10a,
    NVME_SC_FW_NEEDS_CONV_RESET    = 0x10b,
    NVME_SC_INVALID_QUEUE          = 0x10c,
    NVME_SC_FEATURE_NOT_SAVEABLE   = 0x10d,
    NVME_SC_FEATURE_NOT_CHANGEABLE = 0x10e,
    NVME_SC_FEATURE_NOT_PER_NS     = 0x10f,
    NVME_SC_FW_NEEDS_SUBSYS_RESET  = 0x110,
    NVME_SC_FW_NEEDS_RESET         = 0x111,
    NVME_SC_FW_NEEDS_MAX_TIME      = 0x112,
    NVME_SC_FW_ACTIVATE_PROHIBITED = 0x113,
    NVME_SC_OVERLAPPING_RANGE      = 0x114,
    NVME_SC_NS_INSUFFICIENT_CAP    = 0x115,
    NVME_SC_NS_ID_UNAVAILABLE      = 0x116,
    NVME_SC_NS_ALREADY_ATTACHED    = 0x118,
    NVME_SC_NS_IS_PRIVATE          = 0x119,
    NVME_SC_NS_NOT_ATTACHED        = 0x11a,
    NVME_SC_THIN_PROV_NOT_SUPP     = 0x11b,
    NVME_SC_CTRL_LIST_INVALID      = 0x11c,
    NVME_SC_SELF_TEST_IN_PROGRESS  = 0x11d,
    NVME_SC_BP_WRITE_PROHIBITED    = 0x11e,
    NVME_SC_CTRL_ID_INVALID        = 0x11f,
    NVME_SC_SEC_CTRL_STATE_INVALID = 0x120,
    NVME_SC_CTRL_RES_NUM_INVALID   = 0x121,
    NVME_SC_RES_ID_INVALID         = 0x122,
    NVME_SC_PMR_SAN_PROHIBITED     = 0x123,
    NVME_SC_ANA_GROUP_ID_INVALID   = 0x124,
    NVME_SC_ANA_ATTACH_FAILED      = 0x125,

    /*
     * I/O Command Set Specific - NVM commands:
     */
    NVME_SC_BAD_ATTRIBUTES        = 0x180,
    NVME_SC_INVALID_PI            = 0x181,
    NVME_SC_READ_ONLY             = 0x182,
    NVME_SC_CMD_SIZE_LIM_EXCEEDED = 0x183,

    /*
     * I/O Command Set Specific - Fabrics commands:
     */
    NVME_SC_CONNECT_FORMAT        = 0x180,
    NVME_SC_CONNECT_CTRL_BUSY     = 0x181,
    NVME_SC_CONNECT_INVALID_PARAM = 0x182,
    NVME_SC_CONNECT_RESTART_DISC  = 0x183,
    NVME_SC_CONNECT_INVALID_HOST  = 0x184,

    NVME_SC_DISCOVERY_RESTART = 0x190,
    NVME_SC_AUTH_REQUIRED     = 0x191,

    /*
     * I/O Command Set Specific - Zoned commands:
     */
    NVME_SC_ZONE_BOUNDARY_ERROR     = 0x1b8,
    NVME_SC_ZONE_FULL               = 0x1b9,
    NVME_SC_ZONE_READ_ONLY          = 0x1ba,
    NVME_SC_ZONE_OFFLINE            = 0x1bb,
    NVME_SC_ZONE_INVALID_WRITE      = 0x1bc,
    NVME_SC_ZONE_TOO_MANY_ACTIVE    = 0x1bd,
    NVME_SC_ZONE_TOO_MANY_OPEN      = 0x1be,
    NVME_SC_ZONE_INVALID_TRANSITION = 0x1bf,

    /*
     * Media and Data Integrity Errors:
     */
    NVME_SCT_MEDIA_ERROR    = 0x200,
    NVME_SC_WRITE_FAULT     = 0x280,
    NVME_SC_READ_ERROR      = 0x281,
    NVME_SC_GUARD_CHECK     = 0x282,
    NVME_SC_APPTAG_CHECK    = 0x283,
    NVME_SC_REFTAG_CHECK    = 0x284,
    NVME_SC_COMPARE_FAILED  = 0x285,
    NVME_SC_ACCESS_DENIED   = 0x286,
    NVME_SC_UNWRITTEN_BLOCK = 0x287,

    /*
     * Path-related Errors:
     */
    NVME_SCT_PATH               = 0x300,
    NVME_SC_INTERNAL_PATH_ERROR = 0x300,
    NVME_SC_ANA_PERSISTENT_LOSS = 0x301,
    NVME_SC_ANA_INACCESSIBLE    = 0x302,
    NVME_SC_ANA_TRANSITION      = 0x303,
    NVME_SC_CTRL_PATH_ERROR     = 0x360,
    NVME_SC_HOST_PATH_ERROR     = 0x370,
    NVME_SC_HOST_ABORTED_CMD    = 0x371,

    NVME_SC_MASK     = 0x00ff, /* Status Code */
    NVME_SCT_MASK    = 0x0700, /* Status Code Type */
    NVME_SCT_SC_MASK = NVME_SCT_MASK | NVME_SC_MASK,

    NVME_STATUS_CRD  = 0x1800, /* Command Retry Delayed */
    NVME_STATUS_MORE = 0x2000,
    NVME_STATUS_DNR  = 0x4000, /* Do Not Retry */
} nvme_status_field;

static const char* const nvme_statuses[] = {
    [NVME_SC_SUCCESS]                       = "Success",
    [NVME_SC_INVALID_OPCODE]                = "Invalid Command Opcode",
    [NVME_SC_INVALID_FIELD]                 = "Invalid Field in Command",
    [NVME_SC_CMDID_CONFLICT]                = "Command ID Conflict",
    [NVME_SC_DATA_XFER_ERROR]               = "Data Transfer Error",
    [NVME_SC_POWER_LOSS]                    = "Commands Aborted due to Power Loss Notification",
    [NVME_SC_INTERNAL]                      = "Internal Error",
    [NVME_SC_ABORT_REQ]                     = "Command Abort Requested",
    [NVME_SC_ABORT_QUEUE]                   = "Command Aborted due to SQ Deletion",
    [NVME_SC_FUSED_FAIL]                    = "Command Aborted due to Failed Fused Command",
    [NVME_SC_FUSED_MISSING]                 = "Command Aborted due to Missing Fused Command",
    [NVME_SC_INVALID_NS]                    = "Invalid Namespace or Format",
    [NVME_SC_CMD_SEQ_ERROR]                 = "Command Sequence Error",
    [NVME_SC_SGL_INVALID_LAST]              = "Invalid SGL Segment Descriptor",
    [NVME_SC_SGL_INVALID_COUNT]             = "Invalid Number of SGL Descriptors",
    [NVME_SC_SGL_INVALID_DATA]              = "Data SGL Length Invalid",
    [NVME_SC_SGL_INVALID_METADATA]          = "Metadata SGL Length Invalid",
    [NVME_SC_SGL_INVALID_TYPE]              = "SGL Descriptor Type Invalid",
    [NVME_SC_CMB_INVALID_USE]               = "Invalid Use of Controller Memory Buffer",
    [NVME_SC_PRP_INVALID_OFFSET]            = "PRP Offset Invalid",
    [NVME_SC_ATOMIC_WU_EXCEEDED]            = "Atomic Write Unit Exceeded",
    [NVME_SC_OP_DENIED]                     = "Operation Denied",
    [NVME_SC_SGL_INVALID_OFFSET]            = "SGL Offset Invalid",
    [NVME_SC_RESERVED]                      = "Reserved",
    [NVME_SC_HOST_ID_INCONSIST]             = "Host Identifier Inconsistent Format",
    [NVME_SC_KA_TIMEOUT_EXPIRED]            = "Keep Alive Timeout Expired",
    [NVME_SC_KA_TIMEOUT_INVALID]            = "Keep Alive Timeout Invalid",
    [NVME_SC_ABORTED_PREEMPT_ABORT]         = "Command Aborted due to Preempt and Abort",
    [NVME_SC_SANITIZE_FAILED]               = "Sanitize Failed",
    [NVME_SC_SANITIZE_IN_PROGRESS]          = "Sanitize In Progress",
    [NVME_SC_SGL_INVALID_GRANULARITY]       = "SGL Data Block Granularity Invalid",
    [NVME_SC_CMD_NOT_SUP_CMB_QUEUE]         = "Command Not Supported for Queue in CMB",
    [NVME_SC_NS_WRITE_PROTECTED]            = "Namespace is Write Protected",
    [NVME_SC_CMD_INTERRUPTED]               = "Command Interrupted",
    [NVME_SC_TRANSIENT_TR_ERR]              = "Transient Transport Error",
    [NVME_SC_ADMIN_COMMAND_MEDIA_NOT_READY] = "Admin Command Media Not Ready",
    [NVME_SC_INVALID_IO_CMD_SET]            = "Invalid IO Command Set",
    [NVME_SC_LBA_RANGE]                     = "LBA Out of Range",
    [NVME_SC_CAP_EXCEEDED]                  = "Capacity Exceeded",
    [NVME_SC_NS_NOT_READY]                  = "Namespace Not Ready",
    [NVME_SC_RESERVATION_CONFLICT]          = "Reservation Conflict",
    [NVME_SC_FORMAT_IN_PROGRESS]            = "Format In Progress",
    [NVME_SC_CQ_INVALID]                    = "Completion Queue Invalid",
    [NVME_SC_QID_INVALID]                   = "Invalid Queue Identifier",
    [NVME_SC_QUEUE_SIZE]                    = "Invalid Queue Size",
    [NVME_SC_ABORT_LIMIT]                   = "Abort Command Limit Exceeded",
    [NVME_SC_ABORT_MISSING]                 = "Reserved", /* XXX */
    [NVME_SC_ASYNC_LIMIT]                   = "Asynchronous Event Request Limit Exceeded",
    [NVME_SC_FIRMWARE_SLOT]                 = "Invalid Firmware Slot",
    [NVME_SC_FIRMWARE_IMAGE]                = "Invalid Firmware Image",
    [NVME_SC_INVALID_VECTOR]                = "Invalid Interrupt Vector",
    [NVME_SC_INVALID_LOG_PAGE]              = "Invalid Log Page",
    [NVME_SC_INVALID_FORMAT]                = "Invalid Format",
    [NVME_SC_FW_NEEDS_CONV_RESET]           = "Firmware Activation Requires Conventional Reset",
    [NVME_SC_INVALID_QUEUE]                 = "Invalid Queue Deletion",
    [NVME_SC_FEATURE_NOT_SAVEABLE]          = "Feature Identifier Not Saveable",
    [NVME_SC_FEATURE_NOT_CHANGEABLE]        = "Feature Not Changeable",
    [NVME_SC_FEATURE_NOT_PER_NS]            = "Feature Not Namespace Specific",
    [NVME_SC_FW_NEEDS_SUBSYS_RESET]         = "Firmware Activation Requires NVM Subsystem Reset",
    [NVME_SC_FW_NEEDS_RESET]                = "Firmware Activation Requires Reset",
    [NVME_SC_FW_NEEDS_MAX_TIME]             = "Firmware Activation Requires Maximum Time Violation",
    [NVME_SC_FW_ACTIVATE_PROHIBITED]        = "Firmware Activation Prohibited",
    [NVME_SC_OVERLAPPING_RANGE]             = "Overlapping Range",
    [NVME_SC_NS_INSUFFICIENT_CAP]           = "Namespace Insufficient Capacity",
    [NVME_SC_NS_ID_UNAVAILABLE]             = "Namespace Identifier Unavailable",
    [NVME_SC_NS_ALREADY_ATTACHED]           = "Namespace Already Attached",
    [NVME_SC_NS_IS_PRIVATE]                 = "Namespace Is Private",
    [NVME_SC_NS_NOT_ATTACHED]               = "Namespace Not Attached",
    [NVME_SC_THIN_PROV_NOT_SUPP]            = "Thin Provisioning Not Supported",
    [NVME_SC_CTRL_LIST_INVALID]             = "Controller List Invalid",
    [NVME_SC_SELF_TEST_IN_PROGRESS]         = "Device Self-test In Progress",
    [NVME_SC_BP_WRITE_PROHIBITED]           = "Boot Partition Write Prohibited",
    [NVME_SC_CTRL_ID_INVALID]               = "Invalid Controller Identifier",
    [NVME_SC_SEC_CTRL_STATE_INVALID]        = "Invalid Secondary Controller State",
    [NVME_SC_CTRL_RES_NUM_INVALID]          = "Invalid Number of Controller Resources",
    [NVME_SC_RES_ID_INVALID]                = "Invalid Resource Identifier",
    [NVME_SC_PMR_SAN_PROHIBITED]            = "Sanitize Prohibited",
    [NVME_SC_ANA_GROUP_ID_INVALID]          = "ANA Group Identifier Invalid",
    [NVME_SC_ANA_ATTACH_FAILED]             = "ANA Attach Failed",
    [NVME_SC_BAD_ATTRIBUTES]                = "Conflicting Attributes",
    [NVME_SC_INVALID_PI]                    = "Invalid Protection Information",
    [NVME_SC_READ_ONLY]                     = "Attempted Write to Read Only Range",
    [NVME_SC_CMD_SIZE_LIM_EXCEEDED]         = "Command Size Limits Exceeded",
    [NVME_SC_ZONE_BOUNDARY_ERROR]           = "Zoned Boundary Error",
    [NVME_SC_ZONE_FULL]                     = "Zone Is Full",
    [NVME_SC_ZONE_READ_ONLY]                = "Zone Is Read Only",
    [NVME_SC_ZONE_OFFLINE]                  = "Zone Is Offline",
    [NVME_SC_ZONE_INVALID_WRITE]            = "Zone Invalid Write",
    [NVME_SC_ZONE_TOO_MANY_ACTIVE]          = "Too Many Active Zones",
    [NVME_SC_ZONE_TOO_MANY_OPEN]            = "Too Many Open Zones",
    [NVME_SC_ZONE_INVALID_TRANSITION]       = "Invalid Zone State Transition",
    [NVME_SC_WRITE_FAULT]                   = "Write Fault",
    [NVME_SC_READ_ERROR]                    = "Unrecovered Read Error",
    [NVME_SC_GUARD_CHECK]                   = "End-to-end Guard Check Error",
    [NVME_SC_APPTAG_CHECK]                  = "End-to-end Application Tag Check Error",
    [NVME_SC_REFTAG_CHECK]                  = "End-to-end Reference Tag Check Error",
    [NVME_SC_COMPARE_FAILED]                = "Compare Failure",
    [NVME_SC_ACCESS_DENIED]                 = "Access Denied",
    [NVME_SC_UNWRITTEN_BLOCK]               = "Deallocated or Unwritten Logical Block",
    [NVME_SC_INTERNAL_PATH_ERROR]           = "Internal Pathing Error",
    [NVME_SC_ANA_PERSISTENT_LOSS]           = "Asymmetric Access Persistent Loss",
    [NVME_SC_ANA_INACCESSIBLE]              = "Asymmetric Access Inaccessible",
    [NVME_SC_ANA_TRANSITION]                = "Asymmetric Access Transition",
    [NVME_SC_CTRL_PATH_ERROR]               = "Controller Pathing Error",
    [NVME_SC_HOST_PATH_ERROR]               = "Host Pathing Error",
    [NVME_SC_HOST_ABORTED_CMD]              = "Host Aborted Command",
};

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
_Static_assert(sizeof(NVMeCommand) == 64, "NVMeCommand size must be exactly 64 bytes");
typedef struct __attribute__((packed)) NVMeCompletion {
    uint32_t command;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cmd_id;
    uint16_t statusPhase;
    // uint16_t phase : 1;
    // uint16_t status : 15;
} NVMeCompletion;
_Static_assert(sizeof(NVMeCompletion) == 16, "NVMeCompletion size must be exactly 16 bytes");
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

static _Atomic(uint32_t) cmdId = 1;

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
    __asm__ volatile("mfence" ::: "memory");
    sq->tail = (sq->tail + 1) % sq->size;
    writeReg(this, 0x1000 + 8 * sq->qid, sq->tail);
    __asm__ volatile("mfence" ::: "memory");
    uint8_t  attempts = 255;
    uint16_t status   = 0;
    while (attempts) {
        uint32_t timeout = 500000;
        while ((cqe->statusPhase & 0x1) != cq->phase && timeout) {
            __asm__("nop" ::: "memory");
            timeout--;
            // error("Current phase is invalid\n");
        }
        // while (timeout) {
        //     __asm__("nop");
        //     timeout--;
        // }
        status = cqe->statusPhase >> 1;
        if (status & NVME_STATUS_DNR) {
            break;
        }
        if ((cqe->statusPhase & 0x1) == cq->phase && cqe->cmd_id == cmd->command_id &&
            cqe->sq_id == sq->qid) {
            break;
        }
        attempts--;
    }
    if (attempts == 0) {
        debug("CQE->status = %015b CQ->PHASE = %01.1b CQE->PHASE = %01.1b CQE->cmd_id = %x "
              "CMD->cmd_id = %x CQE->sq_id = %x SQ->sq_id = %x condition = %s\n",
              cqe->statusPhase >> 1, cq->phase, cqe->statusPhase & 0x1, cqe->cmd_id,
              cmd->command_id, cqe->sq_id, sq->qid,
              ((cqe->statusPhase & 0x1) == cq->phase && cqe->cmd_id == cmd->command_id &&
               cqe->sq_id == sq->qid)
                  ? "true"
                  : "false");
        error("Exhausted attempts (Status %s)\n", nvme_statuses[status & 0xFF]);
    }
    if (status != 0) {
        debug("CQE->status = %015b CQ->PHASE = %01.1b CQE->PHASE = %01.1b CQE->cmd_id = %x "
              "CMD->cmd_id = %x CQE->sq_id = %x SQ->sq_id = %x condition = %s\n",
              cqe->statusPhase >> 1, cq->phase, cqe->statusPhase & 0x1, cqe->cmd_id,
              cmd->command_id, cqe->sq_id, sq->qid,
              ((cqe->statusPhase & 0x1) == cq->phase && cqe->cmd_id == cmd->command_id &&
               cqe->sq_id == sq->qid)
                  ? "true"
                  : "false");
        error("Status was non zero: %015b (%s)\n", status, nvme_statuses[status & 0xFF]);
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
    memset(cmd, 0, sizeof(NVMeCommand));
    cmd->command_id = cmdId++;
    cmd->opcode     = 0x02;
    cmd->nsid       = this->nsid;
    cmd->prp1       = pageAlignBuffer;
    debug("Reading offset %lu with a length of %lu (Bytes %lu)\n", offset, length, length * 512);
    cmd->cwd10  = (uint32_t)offset;
    cmd->cwd11  = (uint32_t)((uint64_t)offset >> 32);
    cmd->cwd12  = (uint16_t)(length - 1);
    cmd->cwd13  = 0;
    bool result = sendCmd(this, this->ioSq, this->ioCq, cmd);
    for (size_t i = 0; i < length * 512; i += PAGE_SIZE) {
        vmmMapPage(vmmGetPML4(0), (size_t)((size_t)pageAlignBuffer + i),
                   (size_t)((size_t)pageAlignBuffer + i),
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW,
                   MAP_UC | MAP_PRESENT | MAP_WT);
    }
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
    cmd->cwd13      = 0;
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
    memset(createIOQueue, 0, sizeof(NVMeCommand));
    createIOQueue->prp1       = this->ioSq->addr;
    createIOQueue->cwd10      = ((uint32_t)this->ioSq->qid) | ((uint32_t)this->ioSq->size << 16);
    createIOQueue->cwd11      = ((uint32_t)this->ioCq->qid << 16) | 1;
    createIOQueue->opcode     = 1;
    createIOQueue->command_id = cmdId++;
    sendCmd(this, this->admSq, this->admCq, createIOQueue);
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
    this->ioCq->phase = 1;
    this->ioSq->phase = 1;
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