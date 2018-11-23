basic_flags := -std=c99 -Wall -Wpedantic -Wextra -pipe
debug_flags := -DDEBUG -O0 -ggdb -feliminate-unused-debug-symbols
release_flags := -DNDEBUG -O2 -s -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fpie -Wl,-pie
library_flags := -lncurses
source_files := main.c

all: debug

build:
	mkdir $@

build/debug build/release: | build
	mkdir $@

.PHONY: debug
debug: | build/debug
	cc $(basic_flags) $(debug_flags) $(source_files) -o build/debug/acro $(library_flags)

.PHONY: release
release: | build/release
	cc $(basic_flags) $(release_flags) $(source_files) -o build/release/acro $(library_flags)

.PHONY: clean
clean:
	rm -rf build
