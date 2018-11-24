A basic starting point for an ncurses C99 program.

## Build

```
make
build/debug/acro
```

## Make

- `make debug` if you make some mistake in the code, it's a lot easier to catch it when building as debug
it also builds the debug symbols into the binary, so you can use a c/c++ debugger (like gdb or lldb) to step through the program and see the source code as it executes
- `make release` will turn most optimizations on and strip out all of the unnecessary stuff which is the one you'd usually use for real or for giving to other people
- `make clean` to blow away the build/ directory
- `make debug_cli` will make only `orca`.
- `make debug_tui` will make only `orca_tui`.