C engine for the ORCΛ programming environment, with a commandline interpreter
and separate curses terminal user interface.

## Prerequisites

### CLI interpreter

libc, POSIX, C99 compiler, `make`. Tested to build on Linux and Mac (gcc,
clang.) No native Windows port yet, but it will probably build with cygwin
already.

### Terminal UI

The above, plus ncurses or ncursesw. (Note: terminal UI doesn't yet do anything
useful.)

## Build

CLI interpreter:

```sh
make [debug or release, default is debug]
```

TUI:

```sh
make [debug_tui or release_tui]
```

The built binary will be placed at `build/[debug or release]/orca`

Clean:
```sh
make clean
```
Removes `build/`

## Run

```sh
orca [-t timesteps] infile
```

You can also make orca read from stdin:
```sh
echo -e "...\na34\n..." | orca -t 1 /dev/stdin
```
