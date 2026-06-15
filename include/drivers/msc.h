#if !defined(__DRIVERS_MSC_H__)
#define __DRIVERS_MSC_H__
#include <kernel/hal/pci/pci.h>
#include <kernel/task/task.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MSCDriverRequest {
    Process* proc;
    void*    buffer;
    uint64_t lba;
    uint32_t length;
    void*    context;
    void (*completeFunction)(struct MSCDriverRequest* this);
    bool done;
    bool read;
} MSCDriverRequest;

typedef struct MSCDriver {
    // This, buffer, offset, length
    bool (*read)(struct MSCDriver*, void*, size_t, size_t);
    // This, buffer, offset, length
    bool (*write)(struct MSCDriver*, const void*, size_t, size_t);
    bool (*wantsPCI)();
    // This, pci device
    void (*init)(struct MSCDriver*, PCIDevice*);
    // This
    void (*deinit)(struct MSCDriver*);
    void (*issueCommand)(struct MSCDriver* this, MSCDriverRequest* request);
    void* driverData;
} MSCDriver;

MSCDriver* loadRAMMSC(void* bytes, size_t size);
MSCDriver* loadMscDriver(PCIDevice* device);

#endif // __DRIVERS_MSC_H__
