basic_flags := -std=c99 -pipe -Wall -Wpedantic -Wextra -Werror=implicit-function-declaration -D_XOPEN_SOURCE_EXTENDED=1
debug_flags := -DDEBUG -O0 -ggdb -feliminate-unused-debug-symbols
sanitize_flags := -fsanitize=address -fsanitize=undefined
# note: -fsanitize=leak not available on at least Mac 10.12
release_flags := -DNDEBUG -O2 -s -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fpie -Wl,-pie
ifeq ($(shell uname -s), Darwin)
library_flags := -lncurses
else
library_flags := -lncursesw
endif
common_source_files := field.c sim.c
tui_source_files := $(common_source_files) tui_main.c
cli_source_files := $(common_source_files) cli_main.c

all: debug

build:
	@mkdir $@

build/debug build/release: | build
	@mkdir $@

.PHONY: debug_cli
debug_cli: | build/debug
	@cc $(basic_flags) $(debug_flags) $(sanitize_flags) $(cli_source_files) -o build/debug/orca $(library_flags)

.PHONY: debug_tui
debug_tui: | build/debug
	@cc $(basic_flags) $(debug_flags) $(sanitize_flags) $(tui_source_files) -o build/debug/orca_tui $(library_flags)

.PHONY: debug
debug: debug_cli

.PHONY: release_cli
release_cli: | build/release
	@cc $(basic_flags) $(release_flags) $(cli_source_files) -o build/release/orca $(library_flags)

.PHONY: release_tui
release_tui: | build/release
	@cc $(basic_flags) $(release_flags) $(tui_source_files) -o build/release/orca_tui $(library_flags)

.PHONY: release
release: release_cli

.PHONY: clean
clean:
	rm -rf build
