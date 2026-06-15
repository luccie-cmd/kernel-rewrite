#include <common/dbg/dbg.h>
#include <drivers/block/ram.h>
#include <stdlib.h>
#include <string.h>

bool RAMMSCRead(MSCDriver* mscDriver, void* buffer, size_t offset, size_t length) {
    RAMMSCData* drvData = (RAMMSCData*)mscDriver->driverData;
    offset *= 512;
    length *= 512;
    if (drvData->size < length) {
        return false;
    }
    memcpy(buffer, (void*)((uintptr_t)drvData->bytes + offset), length);
    return true;
}
bool RAMMSCWrite(MSCDriver* mscDriver, const void* buffer, size_t offset, size_t length) {
    RAMMSCData* drvData = (RAMMSCData*)mscDriver->driverData;
    offset *= 512;
    length *= 512;
    if (drvData->size < length) {
        return false;
    }
    memcpy((void*)((uintptr_t)drvData->bytes + offset), buffer, length);
    return true;
}
bool RAMMSCWantsPCI() {
    return false;
}
void RAMMSCInit(MSCDriver* mscDriver, PCIDevice* pciDev) {
    (void)mscDriver;
    (void)pciDev;
}
void RAMMSCDeinit(MSCDriver* mscDriver) {
    free(mscDriver->driverData);
}

MSCDriver* loadRAMMSC(void* bytes, size_t size) {
    MSCDriver* ret = malloc(sizeof(MSCDriver));
    if (!ret) {
        error("Failed to allocate memory for RAM MSC\n");
    }
    ret->deinit         = RAMMSCDeinit;
    ret->read           = RAMMSCRead;
    ret->write          = RAMMSCWrite;
    ret->wantsPCI       = RAMMSCWantsPCI;
    ret->init           = RAMMSCInit;
    ret->issueCommand   = NULL;
    RAMMSCData* drvData = malloc(sizeof(RAMMSCData));
    if (!drvData) {
        error("Failed to allocate memory for RAM driver data\n");
    }
    drvData->bytes  = bytes;
    drvData->size   = size;
    ret->driverData = (void*)drvData;
    return ret;
}