# Amstrad CPC BASIC ANSI C Translation

This workspace is an external ANSI C translation project for the Amstrad CPC BASIC unassembly source.

## Goals
- Translate the existing Z80 assembly BASIC interpreter into portable ANSI C.
- Preserve module boundaries and original behavior where feasible.
- Provide a runtime model for BASIC memory, program storage, strings, and firmware interfaces.

## Workspace structure
- `src/` - C source files
- `include/` - public headers
- `Makefile` - build configuration

## Build
Run from the workspace root:

    make

## Next steps
1. Expand the runtime model in `src/runtime.c` and `include/runtime.h`.
2. Translate individual assembly modules into equivalent C functions.
3. Implement CPC firmware jumpblock wrappers in `src/firmware.c`.
4. Validate with small BASIC interpreter tests.
