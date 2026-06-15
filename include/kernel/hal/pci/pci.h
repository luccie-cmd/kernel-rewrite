#if !defined(__KERNEL_HAL_PCI_PCI_H__)
#define __KERNEL_HAL_PCI_PCI_H__
#include <common/dynarray.h>
#include <stdint.h>

typedef struct PCIDevice {
    uint16_t bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendorID;
    uint16_t deviceID;
    void*    base;
    uint8_t class;
    uint8_t subClass;
    // TODO: Fill in the rest
} PCIDevice;

dynarray(PCIDevice*) getPCIDevices();
void pciEnableBusmaster(PCIDevice* dev);
uint16_t pciReadConfigWord(PCIDevice* dev, uint8_t offset);
uint32_t pciReadConfig(PCIDevice* dev, uint8_t offset);
void pciWriteConfig(PCIDevice* dev, uint8_t offset, uint32_t val);

#endif // __KERNEL_HAL_PCI_PCI_H__
