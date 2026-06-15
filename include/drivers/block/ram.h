#if !defined(__DRIVERS_BLOCK_RAM_H__)
#define __DRIVERS_BLOCK_RAM_H__
#include <stddef.h>
#include "../msc.h"

typedef struct RAMMSCData {
    void* bytes;
    size_t size;
} RAMMSCData;

bool RAMMSCRead(MSCDriver* mscDriver, void* buffer, size_t offset, size_t length);
bool RAMMSCWrite(MSCDriver* mscDriver, const void* buffer, size_t offset, size_t length);
bool RAMMSCWantsPCI();
void RAMMSCInit(MSCDriver* mscDriver, PCIDevice* pciDev);
void RAMMSCDeinit(MSCDriver* mscDriver);

#endif // __DRIVERS_BLOCK_RAM_H__
