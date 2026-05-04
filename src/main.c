#include <stdio.h>
#include "runtime.h"

int main(void) {
    BasicState state;
    basic_init(&state);
    printf("Amstrad CPC BASIC ANSI C runtime initialized.\n");
    basic_run(&state);
    return 0;
}
