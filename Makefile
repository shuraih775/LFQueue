.PHONY: all build run clean

BUILD_DIR = build
PRODUCER_CORE ?= 0
CONSUMER_CORE ?= 1

all: build

build:
	@if [ ! -d "$(BUILD_DIR)" ]; then \
		cmake -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release; \
	fi
	ninja -C $(BUILD_DIR)

run: build
	taskset -c $(PRODUCER_CORE),$(CONSUMER_CORE) ./$(BUILD_DIR)/bench

clean:
	rm -rf $(BUILD_DIR)

# while running sanity check better to reduce ops to around 100k.(or else it will take around 3-4 mins to complete the test)
check-sanity:
	mkdir -p build_tsan
	cd build_tsan && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=TSAN
	ninja -C build_tsan
	@echo "Running Thread Sanitizer check..."
	setarch $(shell uname -m) -R ./build_tsan/bench