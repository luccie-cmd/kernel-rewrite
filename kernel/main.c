#include <../Limine/limine.h>
#include <common/dbg/dbg.h>
#include <kernel/hal/gdt/gdt.h>
#include <kernel/hal/idt/idt.h>
#include <kernel/hal/irq/irq.h>
#include <kernel/hal/msr.h>
#include <kernel/mmu/mmu.h>
#include <kernel/objects/elf.h>
#include <kernel/task/syscall.h>
#include <kernel/task/task.h>
#include <kernel/vfs/vfs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern void initX64();

struct limine_mp_request mpRequest = {
    .id       = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .flags    = 0,
};

struct limine_module_request __attribute__((section(".limine_requests"))) module_request = {
    .id                    = LIMINE_MODULE_REQUEST_ID,
    .revision              = 0,
    .response              = NULL,
    .internal_module_count = 0,
    .internal_modules      = NULL,
};

// static void BSPinit() {
//     initIOAPIC();
// }

static struct limine_file* loadModule(const char* name) {
    for (size_t i = 0; i < module_request.response->module_count; ++i) {
        if (strcmp(module_request.response->modules[i]->path, name) == 0) {
            return module_request.response->modules[i];
        }
    }
    error("No module with name `%s` could be found!!!\n", name);
}

size_t stdoutWriteHandler(ProcFile* f, const void* buf, size_t count) {
    if ((f->flags & O_RDONLY) == 1) {
        return -1;
    }
    debug("Writing %lu bytes\n", count);
    printf("%.*s", count, buf);
    return count;
}
size_t stdoutReadHandler(ProcFile* f, void* buf, size_t count) {
    (void)f;
    (void)buf;
    (void)count;
    return -1;
}
void stdoutCloseHandler(ProcFile* f) {
    (void)f;
    error("Attempted to close stdout\n");
}

static void mountAndLoadInit() {
    struct limine_file* initfile     = loadModule("/init.img");
    void*               initFileAddr = initfile->address;
    size_t              initFileSize = initfile->size;
    if (!vfsMountFilePart("/tmpboot", initFileAddr, initFileSize, 0)) {
        error("Failed to mount boot():/init.img to /tmpboot\n");
    }
    uint64_t fileIndex = vfsOpen("/tmpboot/init", OPEN_FLAG_READ);
    if (fileIndex == (uint64_t)-1) {
        error("Failed to open init binary\n");
    }
    ElfFile* file = loadElfObject(fileIndex, 0, "/tmpboot/lib/");
    makeNewProcess(INIT_PID, file);
    Process* proc = getParentProc();
    proc->FDs[1]  = malloc(sizeof(ProcFile));
    if (proc->FDs[1] == NULL) {
        error("ERROR WHILE LOADING INIT (Failed to allocate file for stdout)\n");
    }
    proc->FDs[1]->flags    = O_WRONLY;
    proc->FDs[1]->pos      = 0;
    proc->FDs[1]->refcount = 1;
    proc->FDs[1]->type     = FILETYPE_TTY;
    proc->FDs[1]->write    = stdoutWriteHandler;
    proc->FDs[1]->read     = stdoutReadHandler;
    proc->FDs[1]->close    = stdoutCloseHandler;
    // info("Loaded module `%s` (Addr %p size %zu)\n", initfile->path, initFileAddr, initFileSize);
}

static volatile bool initProcLoaded = false;

static void __attribute__((noreturn)) cpuStart(struct limine_mp_info* mpInfo) {
    // Load x64 extensions
    initX64();
    info("CPU %lu started\n", mpInfo->processor_id);
    loadGDT();
    info("Loaded new GDT (CPU %lu)\n", mpInfo->processor_id);
    loadIDT();
    info("Loaded new IDT (CPU %lu)\n", mpInfo->processor_id);
    // if (mpInfo->lapic_id == mpRequest.response->bsp_lapic_id) {
    //     loadDisplayDriver();
    // }
    initLAPIC();
    info("Initialized LAPIC (CPU %lu)\n", mpInfo->processor_id);
    // if (mpInfo->lapic_id == mpRequest.response->bsp_lapic_id) {
    //     BSPinit();
    // }
    initMSRs();
    info("Loaded msrs (CPU %lu)\n", mpInfo->processor_id);
    if (mpInfo->lapic_id == mpRequest.response->bsp_lapic_id) {
        mountAndLoadInit();
        info("Loaded init binary (CPU %lu)\n", mpInfo->processor_id);
        initProcLoaded = true;
    }
    while (!initProcLoaded) {
        __asm__("nop");
    }
    loadGSbase();
    debug("Loaded GS base (CPU %lu)\n", mpInfo->processor_id);
    while (true) {
        nextProc();
    }
    __builtin_unreachable();
}

void __attribute__((noreturn, no_sanitize_address)) KernelMain() {
    initX64();
    if (mpRequest.response == NULL) {
        error("No MP request set");
    }
    for (uint64_t i = 0; i < mpRequest.response->cpu_count; ++i) {
        struct limine_mp_info* info = mpRequest.response->cpus[i];
        if (mpRequest.response->bsp_lapic_id != i) {
            info->goto_address = cpuStart;
        }
    }
    cpuStart(mpRequest.response->cpus[mpRequest.response->bsp_lapic_id]);
    __builtin_unreachable();
}