#include <stdio.h>
#include <string.h>

static char buffer[4096];

int main(void) {
    printf("> \n");
    fflush(stdout);
    int n = (int)fread(buffer, 1, sizeof(buffer), stdin);
    if (!n) {
        printf("Failed to read input: `%s`\n", strerr(n));
        goto fail;
    }
    printf("`%.*s`\n", (int)n, buffer);
fail:
    while (1) {
        __asm__ volatile("nop");
    }
}
