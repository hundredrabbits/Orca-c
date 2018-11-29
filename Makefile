all: debug

.PHONY: debug
debug:
	@./tool build debug

.PHONY: release
release:
	@./tool build release

.PHONY: clean
clean:
	@./tool clean
