#include <drivers/drivers.h>

dynarray(MSCDriver*) getBlockDevices() {
    dynarray(PCIDevice*) pciDevices = getPCIDevices();
    dynarray(MSCDriver*) drvs       = NULL;
    for (size_t i = 0; i < dyn_size(pciDevices); ++i) {
        if (pciDevices[i]->class == 0x1) {
            MSCDriver* drv = loadMscDriver(pciDevices[i]);
            if (drv) {
                dyn_push(drvs, drv);
            }
        }
    }
    return drvs;
}