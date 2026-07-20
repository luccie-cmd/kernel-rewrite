#include <common/dbg/dbg.h>
#include <common/spinlock.h>
#include <kernel/acpi/acpi.h>
#include <kernel/acpi/tables.h>
#include <kernel/hal/pci/pci.h>
#include <kernel/mmu/vmm/vmm.h>

static dynarray(PCIDevice*) devices;
static void*    ecam;
static Spinlock lock;

static uint32_t readConfig(uint16_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uintptr_t base    = (uintptr_t)ecam + (((uintptr_t)bus << 20) | ((uintptr_t)slot << 15) |
                                           ((uintptr_t)function << 12));
    uintptr_t aligned = base + (offset & ~3u);
    return *(volatile uint32_t*)aligned;
}

static void writeConfig(uint16_t bus, uint8_t slot, uint8_t function, uint8_t offset,
                        uint32_t val) {
    uintptr_t base    = (uintptr_t)ecam + (((uintptr_t)bus << 20) | ((uintptr_t)slot << 15) |
                                           ((uintptr_t)function << 12));
    uintptr_t aligned = base + (offset & ~3u);
    *(volatile uint32_t*)aligned = val;
}

static uint16_t readConfigWord(uint16_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t v = readConfig(bus, slot, function, offset);
    return (v >> ((offset & 2u) * 8)) & 0xFFFF;
}

static uint8_t readConfigByte(uint16_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t v = readConfig(bus, slot, function, offset);
    return (v >> ((offset & 3u) * 8)) & 0xFF;
}

static uint16_t getVendor(uint16_t bus, uint8_t slot, uint8_t func) {
    return readConfigWord(bus, slot, func, 0);
}
static uint16_t getDevice(uint16_t bus, uint8_t slot, uint8_t func) {
    return readConfigWord(bus, slot, func, 2);
}
static uint8_t getClassCode(uint16_t bus, uint8_t slot, uint8_t function) {
    return readConfigByte(bus, slot, function, 0xB);
}
static uint8_t getSubClassCode(uint16_t bus, uint8_t slot, uint8_t function) {
    return readConfigByte(bus, slot, function, 0xA);
}

static void loopBus(uint16_t startBus, uint16_t endBus) {
    debug("Looping from %lu-%lu\n", startBus, endBus);
    for (uint16_t bus = startBus; bus < endBus; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendorID = getVendor(bus, slot, func);
                if (vendorID == 0xffff) {
                    continue;
                }
                uint16_t deviceID     = getDevice(bus, slot, func);
                uint8_t  classCode    = getClassCode(bus, slot, func);
                uint8_t  subClassCode = getSubClassCode(bus, slot, func);
                debug("Class 0x%hhx subclass 0x%hhx progIF 0x%hhx\n", classCode, subClassCode,
                      readConfigByte(bus, slot, func, 0x09));
                if (classCode == 0x06) {
                    if (subClassCode == 0x04) {
                        uint32_t buses = readConfig(bus, slot, func, 0x18);
                        uint8_t  start = (buses >> 16) & 0xFF;
                        uint8_t  end   = (buses >> 8) & 0xFF;
                        info("Found PCI-PCI bridge. Looping from bus %hhu to %hu\n", start, end);
                        loopBus(start, end + 1);
                    }
                } else {
                    PCIDevice* dev = malloc(sizeof(PCIDevice));
                    if (!dev) {
                        error("Failed to allocate PCI device\n");
                    }
                    dev->bus      = bus;
                    dev->device   = slot;
                    dev->function = func;
                    dev->vendorID = vendorID;
                    dev->deviceID = deviceID;
                    dev->class    = classCode;
                    dev->subClass = subClassCode;
                    dyn_push(devices, dev);
                }
            }
        }
    }
}

uint16_t pciReadConfigWord(PCIDevice* dev, uint8_t offset) {
    return readConfigWord(dev->bus, dev->device, dev->function, offset);
}

uint32_t pciReadConfig(PCIDevice* dev, uint8_t offset) {
    return readConfig(dev->bus, dev->device, dev->function, offset);
}

void pciWriteConfig(PCIDevice* dev, uint8_t offset, uint32_t val) {
    writeConfig(dev->bus, dev->device, dev->function, offset, val);
}

void pciEnableBusmaster(PCIDevice* dev) {
    uint16_t cmd    = pciReadConfigWord(dev, 0x04);
    uint16_t status = pciReadConfigWord(dev, 0x06);
    cmd |= (1 << 2);
    pciWriteConfig(dev, 0x04, (uint32_t)status << 16 | (uint32_t)cmd);
}

dynarray(PCIDevice*) getPCIDevices() {
    if (devices != NULL) {
        return devices;
    }
    LOCK(lock);
    MCFG* mcfg = acpiGetTable("MCFG");
    if (!mcfg) {
        warn("PCI loading via old methods\n");
    }
    ecam          = (void*)(mcfg->ecams[0].base + (uint64_t)getHHDM());
    uint8_t start = mcfg->ecams[0].start;
    uint8_t end   = mcfg->ecams[0].end;
    loopBus(start, end);
    UNLOCK(lock);
    return devices;
}