# Host build of core/ + tests/ with no Rack SDK.
HOST_FLAGS := -std=c++17 -pthread -O1 -g -Wall -Wextra -Icore -Ivendor -fsanitize=address,undefined -fno-omit-frame-pointer -DSPOOLS_FIXTURES=\"$(CURDIR)/tests/fixtures\"
HOST_SRC := $(wildcard core/*.cpp) $(wildcard tests/*.cpp)
HOST_OBJ := $(patsubst %.cpp,build/host/%.o,$(HOST_SRC))

build/host/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(HOST_FLAGS) -MMD -MP -c $< -o $@

build/host/run-tests: $(HOST_OBJ)
	$(CXX) $(HOST_FLAGS) $^ -o $@

test: build/host/run-tests
	$< $(TEST_ARGS)

.PHONY: test
-include $(HOST_OBJ:.o=.d)

# make bench: release build of core/ plus bench/, with Rack's optimisation flags.
BENCH_FLAGS := -std=c++17 -pthread -O3 -march=nehalem -funsafe-math-optimizations -Icore -Ivendor
bench: $(wildcard core/*.cpp) $(wildcard core/*.hpp) bench/bench.cpp
	@mkdir -p build/bench
	$(CXX) $(BENCH_FLAGS) $(wildcard core/*.cpp) bench/bench.cpp -o build/bench/bench
	build/bench/bench

.PHONY: bench
