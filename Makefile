.PHONY: all build build-clang run run-clang clean \
check-sanity \
dro-asm dro-run perf-dro \
lf-asm lf-run perf-lf \
rigtorp-asm rigtorp-run perf-rigtorp

BUILD_DIR = build
CLANG_BUILD_DIR = build-clang

PRODUCER_CORE ?= 2
CONSUMER_CORE ?= 4

all: build

build:
	@if [ ! -d "$(BUILD_DIR)" ]; then \
		CC=gcc CXX=g++ \
		cmake -B $(BUILD_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=Release; \
	fi
	ninja -C $(BUILD_DIR)

build-clang:
	@if [ ! -d "$(CLANG_BUILD_DIR)" ]; then \
		CC=clang CXX=clang++ \
		cmake -B $(CLANG_BUILD_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=Release; \
	fi
	ninja -C $(CLANG_BUILD_DIR)


run: build
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	./$(BUILD_DIR)/bench \
	$(PRODUCER_CORE) $(CONSUMER_CORE)

run-clang: build-clang
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	./$(CLANG_BUILD_DIR)/bench \
	$(PRODUCER_CORE) $(CONSUMER_CORE)

clean:
	rm -rf $(BUILD_DIR)

check-sanity:
	mkdir -p build_tsan
	cd build_tsan && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=TSAN
	ninja -C build_tsan
	@echo "Running Thread Sanitizer check..."
	setarch $(shell uname -m) -R ./build_tsan/bench

# ---------------- DRO ASM ----------------

dro-asm: build
	ninja -C $(BUILD_DIR) dro_asm_bench
	@echo "Generated:"
	@echo " - $(BUILD_DIR)/dro_dump.asm"

dro-asm-clang: build-clang
	ninja -C $(CLANG_BUILD_DIR) dro_asm_bench
	@echo "Generated:"
	@echo " - $(CLANG_BUILD_DIR)/dro_dump.asm"

dro-run: dro-asm
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) ./$(BUILD_DIR)/dro_asm_bench $(PRODUCER_CORE) $(CONSUMER_CORE)

perf-dro: dro-asm
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	perf stat -e cycles,instructions,branches,branch-misses,cache-misses \
	./$(BUILD_DIR)/dro_asm_bench

perf-dro-clang: dro-asm-clang
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	perf stat -e \
	cycles,instructions,branches,branch-misses,cache-misses \
	./$(CLANG_BUILD_DIR)/dro_asm_bench

# ---------------- LFQUEUE ASM ----------------

lf-asm: build
	ninja -C $(BUILD_DIR) lfqueue_asm_bench
	@echo "Generated:"
	@echo " - $(BUILD_DIR)/lfqueue_dump.asm"

lf-asm-clang: build-clang
	ninja -C $(CLANG_BUILD_DIR) lfqueue_asm_bench
	@echo "Generated:"
	@echo " - $(CLANG_BUILD_DIR)/lfqueue_dump.asm"
lf-run: lf-asm
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) ./$(BUILD_DIR)/lfqueue_asm_bench $(PRODUCER_CORE) $(CONSUMER_CORE)

perf-lf: lf-asm
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	perf stat -e cycles,instructions,branches,branch-misses,cache-misses \
	./$(BUILD_DIR)/lfqueue_asm_bench

perf-lf-clang: lf-asm-clang
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	perf stat -e \
	cycles,instructions,branches,branch-misses,cache-misses \
	./$(CLANG_BUILD_DIR)/lfqueue_asm_bench

#rigtorp asm
rigtorp-asm: build
	ninja -C $(BUILD_DIR) rigtorp_asm_bench
	@echo "Generated:"
	@echo " - $(BUILD_DIR)/rigtorp_dump.asm"

rigtorp-asm-clang: build-clang
	ninja -C $(CLANG_BUILD_DIR) rigtorp_asm_bench
	@echo "Generated:"
	@echo " - $(CLANG_BUILD_DIR)/rigtorp_dump.asm"

rigtorp-run: rigtorp-asm
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) ./$(BUILD_DIR)/rigtorp_asm_bench $(PRODUCER_CORE) $(CONSUMER_CORE)

perf-rigtorp: rigtorp-asm
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	perf stat -e cycles,instructions,branches,branch-misses,cache-misses \
	./$(BUILD_DIR)/rigtorp_asm_bench

perf-rigtorp-clang: rigtorp-asm-clang
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) \
	perf stat -e \
	cycles,instructions,branches,branch-misses,cache-misses \
	./$(CLANG_BUILD_DIR)/rigtorp_asm_bench