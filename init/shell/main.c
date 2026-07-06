#include <stdio.h>

int main() {
    printf("Fuck you sideways\n");
    while (1) {
        __asm__ volatile("nop");
    }
}