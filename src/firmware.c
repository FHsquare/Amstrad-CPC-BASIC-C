#include <stdio.h>
#include "runtime.h"

void firmware_init(void) {
    // Initialize firmware API stubs and any host-specific state.
}

void firmware_call_stub(const char *name) {
    printf("[firmware stub] %s\n", name);
}
