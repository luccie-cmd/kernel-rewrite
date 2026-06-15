#if !defined(__KERNEL_HAL_IDT_IDT_H__)
#define __KERNEL_HAL_IDT_IDT_H__
#include <stdint.h>

typedef struct __attribute__((packed)) IDTEntry {
    uint16_t offset0;
    uint16_t segment_sel;
    uint8_t  ist : 3;
    uint8_t  reserved0 : 5;
    uint8_t  gate_type : 4;
    uint8_t  zero : 1;
    uint8_t  dpl : 2;
    uint8_t  present : 1;
    uint16_t offset1;
    uint32_t offset2;
    uint32_t reserved1;
} IDTEntry;

#define IDT_GATE_TYPE_INTERRUPT 0xE
#define IDT_GATE_TYPE_TRAP_GATE 0xF

#define IDT_ENTRY(offset, segment, type, dpl, ist)                                                 \
    (IDTEntry){                                                                                    \
        (uint16_t)((offset) & 0xFFFF),             /* offset0 */                                   \
        (segment),                                 /* segment_sel */                               \
        (ist),                                     /* ist */                                       \
        0,                                         /* reserved0 */                                 \
        (type),                                    /* gate_type */                                 \
        0,                                         /* zero */                                      \
        (dpl),                                     /* dpl */                                       \
        1,                                         /* present */                                   \
        (uint16_t)(((offset) >> 16) & 0xFFFF),     /* offset1 */                                   \
        (uint32_t)(((offset) >> 32) & 0xFFFFFFFF), /* offset2 */                                   \
        0                                          /* reserved1 */                                 \
    }

void loadIDT();
void idtLoadGates();
void idtEnableGate(uint8_t gate);
void disablePFProtection();
void enablePFProtection();
// void disableUDProtection();
// void enableUDProtection();
// void disableBPProtection();
// void enableBPProtection();
// void disableGPProtection();
// void enableGPProtection();
void idtRegisterHandler(uint8_t gate, void* function, uint8_t type);

#endif // __KERNEL_HAL_IDT_IDT_H__
