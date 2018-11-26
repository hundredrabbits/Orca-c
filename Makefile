basic_flags := -std=c99 -pipe -Wall -Wpedantic -Wextra -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -Wconversion -D_XOPEN_SOURCE_EXTENDED=1
debug_flags := -DDEBUG -ggdb
sanitize_flags := -fsanitize=address -fsanitize=undefined
# note: -fsanitize=leak not available on at least Mac 10.12
release_flags := -DNDEBUG -O2 -s -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fpie -Wl,-pie
cli_library_flags :=
ifeq ($(shell uname -s), Darwin)
debug_flags := $(debug_flags) -O1
tui_library_flags := -lncurses
else
debug_flags := $(debug_flags) -Og -feliminate-unused-debug-symbols
tui_library_flags := -lncursesw
endif
common_source_files := field.c mark.c sim.c
tui_source_files := $(common_source_files) tui_main.c
cli_source_files := $(common_source_files) cli_main.c

all: debug

build:
	@mkdir $@

build/debug build/release: | build
	@mkdir $@

.PHONY: debug_cli
debug_cli: | build/debug
	@cc $(basic_flags) $(debug_flags) $(sanitize_flags) $(cli_source_files) -o build/debug/orca $(cli_library_flags)

.PHONY: debug_tui
debug_tui: | build/debug
	@cc $(basic_flags) $(debug_flags) $(sanitize_flags) $(tui_source_files) -o build/debug/orca_tui $(tui_library_flags)

.PHONY: debug
debug: debug_cli

.PHONY: release_cli
release_cli: | build/release
	@cc $(basic_flags) $(release_flags) $(cli_source_files) -o build/release/orca $(cli_library_flags)

.PHONY: release_tui
release_tui: | build/release
	@cc $(basic_flags) $(release_flags) $(tui_source_files) -o build/release/orca_tui $(tui_library_flags)

.PHONY: release
release: release_cli

.PHONY: clean
clean:
	rm -rf build
