#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COUNT 32

static struct fl {
    struct fl* next;
    void (*f[COUNT])(void*);
    void* a[COUNT];
} builtin, *head;

static bool    finished_atexit;
static uint8_t slot;

int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    (void)dso;
    if (finished_atexit) {
        return -1;
    }
    if (!head) head = &builtin;
    if (slot == COUNT) {
        struct fl* new_fl = malloc(sizeof(struct fl) * 1);
        if (!new_fl) {
            return -1;
        }
        memset(new_fl, 0, sizeof(struct fl));
        new_fl->next = head;
        head         = new_fl;
        slot         = 0;
    }
    head->f[slot] = func;
    head->a[slot] = arg;
    slot++;
    return 0;
}

void __run_atexit(void) {
    struct fl* current = head;
    struct fl* next_block;
    finished_atexit = true;
    while (current) {
        int current_slot = (current == head) ? slot : COUNT;
        while (current_slot-- > 0) {
            if (current->f[current_slot]) {
                current->f[current_slot](current->a[current_slot]);
                current->f[current_slot] = NULL;
            }
        }
        next_block = current->next;
        if (current != &builtin) {
            free(current);
        }
        current = next_block;
    }
    head = NULL;
    slot = 0;
}

static void call(void* p) {
    ((void (*)(void))(uintptr_t)p)();
}

int atexit(void (*func)(void)) {
    return __cxa_atexit(call, (void*)(uintptr_t)func, 0);
}