C engine for the ORCΛ programming environment, with a commandline interpreter.

## Prerequisites

POSIX, C99 compiler, `bash` for the build script. Tested to build on Linux and
Mac with GCC and clang. No native Windows port yet, but it will probably
already build under cygwin.

## Build

You can use the build script directly, or with the `make` wrapper.

### Make

```sh
make [debug or release, default is debug]
```

The built binary will be placed at `build/[debug or release]/orca`

Clean:
```sh
make clean
```
Removes `build/`

### Build Script

Run `./tool --help` to see usage info.

## Build Tui

```sh
./tool build debug tui
```

## Run

```sh
orca [-t timesteps] infile
```

You can also make orca read from stdin:
```sh
echo -e "...\na34\n..." | orca /dev/stdin
```
