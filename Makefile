# Convenience wrapper around CMake/CTest workflows for conduit.
# Real build system is CMake — this just memorizes common invocations.

BUILD_DIR        ?= build
BUILD_TYPE       ?= Debug
JOBS             ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
GENERATOR        ?=
CMAKE            ?= cmake
CTEST            ?= ctest
LLVM_PROFDATA    ?= xcrun llvm-profdata
LLVM_COV         ?= xcrun llvm-cov

CMAKE_GEN_FLAG   := $(if $(GENERATOR),-G "$(GENERATOR)",)

# Turn on every transport adapter. Passed to `configure`/`tidy`/`sanitize`/
# `release`/`coverage` so the core targets exercise every adapter, not just
# the in-process bus.
TRANSPORT_FLAGS  := \
    -DCONDUIT_TRANSPORT_MQTT=ON \
    -DCONDUIT_TRANSPORT_AMQP=ON \
    -DCONDUIT_TRANSPORT_REDIS=ON \
    -DCONDUIT_TRANSPORT_NATS=ON \
    -DCONDUIT_TRANSPORT_ZMQ=ON

.DEFAULT_GOAL := help

.PHONY: help
help:
	@echo "conduit — make targets"
	@echo ""
	@echo "Core build targets enable every transport adapter (MQTT/AMQP/NATS/Redis/ZMQ)."
	@echo ""
	@echo "  make configure         Configure $(BUILD_DIR)/ ($(BUILD_TYPE)) with all transports"
	@echo "  make build             Build everything in $(BUILD_DIR)/"
	@echo "  make test              Run ctest in $(BUILD_DIR)/"
	@echo "  make example           Run the conduit_hello example"
	@echo "  make examples          Build and run every conduit_* example"
	@echo "  make all               configure + build + test"
	@echo ""
	@echo "  make sanitize          Configure+build+test in build-san/ with ASan+UBSan"
	@echo "  make tidy              Configure+build in build-tidy/ with clang-tidy"
	@echo "  make release           Configure+build in build-release/ (Release)"
	@echo "  make coverage          Configure+build+test in build-coverage/ with Clang coverage"
	@echo "  make docs              Configure+build Doxygen HTML in build-docs/"
	@echo ""
	@echo "Per-broker targets (used by CI against a live containerized broker; the"
	@echo "core build already exercises every adapter against the in-process tests)."
	@echo ""
	@echo "  make mqtt              Configure+build+test in build-mqtt/ with only MQTT enabled"
	@echo "  make amqp              Configure+build+test in build-amqp/ with only AMQP enabled"
	@echo "  make nats              Configure+build+test in build-nats/ with only NATS enabled"
	@echo "  make redis             Configure+build+test in build-redis/ with only Redis enabled"
	@echo "  make zmq               Configure+build+test in build-zmq/ with only ZeroMQ enabled"
	@echo ""
	@echo "  make format            Run clang-format -i over project sources"
	@echo "  make format-check      Verify formatting without writing"
	@echo ""
	@echo "  make ci                Pre-push gate: format-check + tidy + test + sanitize + release"
	@echo ""
	@echo "  make clean             Remove $(BUILD_DIR)/"
	@echo "  make distclean         Remove all build-* directories"
	@echo ""
	@echo "Variables: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS)"

.PHONY: configure
configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(TRANSPORT_FLAGS)

.PHONY: build
build: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

.PHONY: test
test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

.PHONY: example
example: build
	$(BUILD_DIR)/examples/conduit_hello

.PHONY: examples
examples: build
	@set -e; \
	failures=""; \
	for ex in $(BUILD_DIR)/examples/conduit_*; do \
		if [ -x "$$ex" ] && [ ! -d "$$ex" ]; then \
			name=$$(basename "$$ex"); \
			echo "=== $$name ==="; \
			if ! "$$ex" >/dev/null; then \
				echo "FAIL: $$name (exit $$?)"; \
				failures="$$failures $$name"; \
			fi; \
		fi; \
	done; \
	if [ -n "$$failures" ]; then \
		echo "Examples that failed:$$failures"; \
		exit 1; \
	fi

.PHONY: all
all: test

.PHONY: ci
ci: format-check tidy test sanitize release
	@echo ""
	@echo "ci: all checks passed"

.PHONY: sanitize
sanitize:
	$(CMAKE) -S . -B build-san $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCONDUIT_ENABLE_SANITIZERS=ON $(TRANSPORT_FLAGS)
	$(CMAKE) --build build-san -j $(JOBS)
	$(CTEST) --test-dir build-san --output-on-failure

.PHONY: tidy
tidy:
	$(CMAKE) -S . -B build-tidy $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCONDUIT_ENABLE_CLANG_TIDY=ON $(TRANSPORT_FLAGS)
	$(CMAKE) --build build-tidy -j $(JOBS)

.PHONY: release
release:
	$(CMAKE) -S . -B build-release $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Release $(TRANSPORT_FLAGS)
	$(CMAKE) --build build-release -j $(JOBS)
	$(CTEST) --test-dir build-release --output-on-failure

# Per-broker targets — used by CI jobs that host a real broker service.
# Kept out of `make ci` because the core build already exercises every
# adapter against the in-process smoke tests.

.PHONY: mqtt
mqtt:
	$(CMAKE) -S . -B build-mqtt $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCONDUIT_TRANSPORT_MQTT=ON
	$(CMAKE) --build build-mqtt -j $(JOBS)
	$(CTEST) --test-dir build-mqtt --output-on-failure

.PHONY: amqp
amqp:
	$(CMAKE) -S . -B build-amqp $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCONDUIT_TRANSPORT_AMQP=ON
	$(CMAKE) --build build-amqp -j $(JOBS)
	$(CTEST) --test-dir build-amqp --output-on-failure

.PHONY: nats
nats:
	$(CMAKE) -S . -B build-nats $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCONDUIT_TRANSPORT_NATS=ON
	$(CMAKE) --build build-nats -j $(JOBS)
	$(CTEST) --test-dir build-nats --output-on-failure

.PHONY: redis
redis:
	$(CMAKE) -S . -B build-redis $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCONDUIT_TRANSPORT_REDIS=ON
	$(CMAKE) --build build-redis -j $(JOBS)
	$(CTEST) --test-dir build-redis --output-on-failure

.PHONY: zmq
zmq:
	$(CMAKE) -S . -B build-zmq $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCONDUIT_TRANSPORT_ZMQ=ON
	$(CMAKE) --build build-zmq -j $(JOBS)
	$(CTEST) --test-dir build-zmq --output-on-failure

.PHONY: coverage
coverage:
	$(CMAKE) -S . -B build-coverage $(CMAKE_GEN_FLAG) \
		-DCMAKE_BUILD_TYPE=Debug -DCONDUIT_ENABLE_COVERAGE=ON $(TRANSPORT_FLAGS)
	$(CMAKE) --build build-coverage -j $(JOBS)
	rm -f build-coverage/*.profraw build-coverage/conduit.profdata
	LLVM_PROFILE_FILE="$(CURDIR)/build-coverage/conduit-%p.profraw" \
		$(CTEST) --test-dir build-coverage --output-on-failure
	$(LLVM_PROFDATA) merge -sparse build-coverage/*.profraw \
		-o build-coverage/conduit.profdata
	$(LLVM_COV) report build-coverage/tests/conduit_tests \
		-instr-profile=build-coverage/conduit.profdata \
		-ignore-filename-regex='(_deps|tests)/'
	$(LLVM_COV) show build-coverage/tests/conduit_tests \
		-instr-profile=build-coverage/conduit.profdata \
		-ignore-filename-regex='(_deps|tests)/' \
		-format=html -output-dir=build-coverage/coverage-html \
		-show-line-counts-or-regions
	@echo "HTML report: build-coverage/coverage-html/index.html"

.PHONY: docs
docs:
	$(CMAKE) -S . -B build-docs $(CMAKE_GEN_FLAG) \
	    -DCONDUIT_BUILD_DOCS=ON \
	    -DCONDUIT_BUILD_TESTS=OFF \
	    -DCONDUIT_BUILD_EXAMPLES=OFF
	$(CMAKE) --build build-docs --target conduit_docs -j $(JOBS)
	@echo "HTML report: build-docs/docs/html/index.html"

FORMAT_FILES := $(shell find include tests examples transports -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' \) 2>/dev/null)

.PHONY: format
format:
	@if [ -z "$(FORMAT_FILES)" ]; then echo "no source files found"; else clang-format -i $(FORMAT_FILES); fi

.PHONY: format-check
format-check:
	@if [ -z "$(FORMAT_FILES)" ]; then echo "no source files found"; else clang-format --dry-run --Werror $(FORMAT_FILES); fi

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean:
	rm -rf build build-san build-tidy build-release build-coverage build-docs \
	       build-mqtt build-amqp build-redis build-nats build-zmq cmake-build-*
