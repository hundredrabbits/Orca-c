all: debug

.PHONY: debug
debug:
	@./tool build debug orca

.PHONY: release
release:
	@./tool build release orca

.PHONY: clean
clean:
	@./tool clean
