# Zinc

A statically-typed, expression-oriented language that transpiles to C.

## Quick Start

```bash
make            # Build the compiler
./build/zinc -c program.zn -o program
./program
```

## Building

Requires GCC, Flex, and Bison.

```bash
make            # Build the compiler (build/zinc)
make test-all   # Run all tests
make clean      # Clean build artifacts
```
