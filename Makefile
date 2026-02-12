CC = gcc
CFLAGS = -Wall -g

# Homebrew keg-only paths (macOS)
FLEX = /opt/homebrew/opt/flex/bin/flex
BISON = /opt/homebrew/opt/bison/bin/bison

# Directories
SRC_DIR = src
BUILD_DIR = build
TEST_PASS_DIR = test/pass
TEST_FAIL_DIR = test/fail
TEST_LEAK_DIR = test/leak

all: $(BUILD_DIR)/zinc $(BUILD_DIR)/zinc_runtime.h

$(BUILD_DIR)/zinc: $(BUILD_DIR)/parser.c $(BUILD_DIR)/scanner.c $(SRC_DIR)/ast.c $(SRC_DIR)/semantic.c $(SRC_DIR)/codegen.c $(SRC_DIR)/codegen_expr.c $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(BUILD_DIR) -o $@ $(BUILD_DIR)/parser.c $(BUILD_DIR)/scanner.c $(SRC_DIR)/ast.c $(SRC_DIR)/semantic.c $(SRC_DIR)/codegen.c $(SRC_DIR)/codegen_expr.c $(SRC_DIR)/main.c

$(BUILD_DIR)/parser.c $(BUILD_DIR)/parser.h: $(SRC_DIR)/parser.y | $(BUILD_DIR)
	$(BISON) -d -v -o $(BUILD_DIR)/parser.c $(SRC_DIR)/parser.y

$(BUILD_DIR)/scanner.c $(BUILD_DIR)/scanner.h: $(SRC_DIR)/scanner.l $(BUILD_DIR)/parser.h | $(BUILD_DIR)
	$(FLEX) --outfile=$(BUILD_DIR)/scanner.c --header-file=$(BUILD_DIR)/scanner.h $(SRC_DIR)/scanner.l

# Copy runtime header to build dir (compiler copies it to output at runtime)
$(BUILD_DIR)/zinc_runtime.h: $(SRC_DIR)/zinc_runtime.h | $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

test: $(BUILD_DIR)/zinc
	@passed=0; failed=0; \
	echo ""; \
	echo "========================================"; \
	echo "Running valid program tests..."; \
	echo "========================================"; \
	for f in $(TEST_PASS_DIR)/*.zn; do \
		name=$$(basename $$f); \
		if ./$(BUILD_DIR)/zinc --ast "$$f" > /dev/null 2>&1; then \
			echo "  PASS: $$name"; \
			passed=$$((passed + 1)); \
		else \
			echo "  FAIL: $$name (expected to parse successfully)"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "========================================"; \
	echo "Running error recovery tests..."; \
	echo "========================================"; \
	for f in $(TEST_FAIL_DIR)/*.zn; do \
		name=$$(basename $$f); \
		expected=$$(grep '^# ERRORS:' "$$f" | head -1 | sed 's/# ERRORS: *//'); \
		output=$$(./$(BUILD_DIR)/zinc --check "$$f" 2>&1); \
		exitcode=$$?; \
		parse_errs=$$(echo "$$output" | grep -o '[0-9]* parse error(s)' | grep -o '[0-9]*'); \
		semantic_errs=$$(echo "$$output" | grep -o '[0-9]* semantic error(s)' | grep -o '[0-9]*'); \
		actual=$$((0 + $${parse_errs:-0} + $${semantic_errs:-0})); \
		if [ "$$exitcode" -eq 1 ] && [ "$$actual" = "$$expected" ]; then \
			echo "  PASS: $$name ($$actual errors as expected)"; \
			passed=$$((passed + 1)); \
		elif [ "$$exitcode" -eq 0 ]; then \
			echo "  FAIL: $$name (parsed successfully, expected $$expected errors)"; \
			failed=$$((failed + 1)); \
		else \
			echo "  FAIL: $$name (got $$actual errors, expected $$expected)"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "========================================"; \
	echo "Test Summary: $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ "$$failed" -gt 0 ]; then exit 1; fi

# Transpiler tests - compile .zn files to executables and run them
test-transpile: $(BUILD_DIR)/zinc $(BUILD_DIR)/zinc_runtime.h
	@passed=0; failed=0; \
	echo ""; \
	echo "========================================"; \
	echo "Running transpiler tests..."; \
	echo "========================================"; \
	mkdir -p /tmp/zinc-test; \
	for f in $(TEST_PASS_DIR)/*.zn; do \
		name=$$(basename $$f .zn); \
		outdir="/tmp/zinc-test"; \
		if ./$(BUILD_DIR)/zinc -c "$$f" -o "$$outdir/$$name" > /dev/null 2>&1; then \
			if [ -x "$$outdir/$$name" ]; then \
				"$$outdir/$$name" > /dev/null 2>&1; \
				if [ $$? -eq 0 ]; then \
					echo "  PASS: $$name (transpiled, compiled, ran)"; \
					passed=$$((passed + 1)); \
				else \
					echo "  FAIL: $$name (runtime error)"; \
					failed=$$((failed + 1)); \
				fi; \
			else \
				echo "  FAIL: $$name (executable not created)"; \
				failed=$$((failed + 1)); \
			fi; \
		else \
			echo "  FAIL: $$name (transpilation/compilation failed)"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	rm -rf /tmp/zinc-test; \
	echo ""; \
	echo "========================================"; \
	echo "Transpiler Summary: $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ "$$failed" -gt 0 ]; then exit 1; fi

# Leak tests - compile .zn files, run through macOS leaks --atExit
test-leaks: $(BUILD_DIR)/zinc $(BUILD_DIR)/zinc_runtime.h
	@if ! command -v leaks > /dev/null 2>&1; then \
		echo ""; \
		echo "========================================"; \
		echo "Skipping leak tests (leaks command not available)"; \
		echo "========================================"; \
		exit 0; \
	fi; \
	if ! ls $(TEST_LEAK_DIR)/*.zn > /dev/null 2>&1; then \
		echo ""; \
		echo "========================================"; \
		echo "Leak Test Summary: 0 passed, 0 failed"; \
		echo "========================================"; \
		exit 0; \
	fi; \
	passed=0; failed=0; \
	echo ""; \
	echo "========================================"; \
	echo "Running leak tests..."; \
	echo "========================================"; \
	mkdir -p /tmp/zinc-test; \
	for f in $(TEST_LEAK_DIR)/*.zn; do \
		name=$$(basename $$f .zn); \
		outdir="/tmp/zinc-test"; \
		if ./$(BUILD_DIR)/zinc -c "$$f" -o "$$outdir/$$name" > /dev/null 2>&1; then \
			if [ -x "$$outdir/$$name" ]; then \
				output=$$(leaks --atExit -- "$$outdir/$$name" 2>&1); \
				if echo "$$output" | grep -q "0 leaks for 0 total leaked bytes"; then \
					echo "  PASS: $$name (0 leaks)"; \
					passed=$$((passed + 1)); \
				else \
					leak_count=$$(echo "$$output" | grep "leaks for" | sed 's/.*: *\([0-9]*\) leaks.*/\1/'); \
					echo "  FAIL: $$name ($$leak_count leaks detected)"; \
					failed=$$((failed + 1)); \
				fi; \
			else \
				echo "  FAIL: $$name (executable not created)"; \
				failed=$$((failed + 1)); \
			fi; \
		else \
			echo "  FAIL: $$name (transpilation/compilation failed)"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	rm -rf /tmp/zinc-test; \
	echo ""; \
	echo "========================================"; \
	echo "Leak Test Summary: $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ "$$failed" -gt 0 ]; then exit 1; fi

# Run all tests
test-all: test test-transpile test-leaks

.PHONY: all clean test test-transpile test-leaks test-all
