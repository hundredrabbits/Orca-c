basic_flags := -std=c99 -Wall -Wpedantic -Wextra
debug_flags := -DDEBUG -O0 -ggdb -feliminate-unused-debug-symbols
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

.PHONY: clean
clean:
	rm -rf build
