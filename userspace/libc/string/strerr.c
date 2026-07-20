#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* strerr(int errnoN) {
    switch (errnoN) {
    default: {
        printf("TODO: Handle errnoN %d in strerr\n", errnoN);
        exit(2);
    } break;
    }
}