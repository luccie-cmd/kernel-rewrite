#include <drivers/block/nvme.h>
#include <drivers/msc.h>

MSCDriver* loadMscDriver(PCIDevice* dev) {
    MSCDriver* drv = NULL;
    switch (dev->subClass) {
    case 6: {
        warn("Useless device detected\n");
    } break;
    case 8: {
        drv = loadNVMeDriver();
    } break;
    default: {
        todo(true, "Add support for subclass %lu\n", dev->subClass);
    } break;
    }
    if (drv) {
        drv->init(drv, drv->wantsPCI() ? dev : NULL);
    }
    return drv;
}