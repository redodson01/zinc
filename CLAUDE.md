# Zinc — Project Guide

## Build & Test

```bash
make            # Build build/zinc
make test-all   # Run all tests (pass + fail + transpile + leaks)
make test-leaks # Run leak tests only (macOS, uses leaks --atExit)
make clean      # Clean build artifacts
```

## Language Overview

Zinc is a statically-typed, expression-oriented language that transpiles to C. It uses automatic reference counting (ARC) for memory management and type inference to minimize annotations. The README has the full language guide.

## Architecture

### Compilation Pipeline

```
.zn source → Scanner (Flex) → Parser (Bison) → AST → Semantic Analysis → Code Generation → .c/.h → GCC → executable
```

### Source Files (`src/`)

| File | Role |
|------|------|
| `main.c` | Driver: argument parsing, orchestrates pipeline phases |
| `scanner.l` | Flex lexer with string interpolation state machine |
| `scanner_extra.h` | Scanner context struct shared between scanner and parser |
| `parser.y` | Bison grammar; parse-time desugaring (`unless`→`if !`, `until`→`while !`, interpolation→concat) |
| `ast.h` / `ast.c` | AST node types, type system representation, constructors, AST printer |
| `semantic.h` / `semantic.c` | Type inference, type checking, symbol tables, scope management, error reporting |
| `codegen.h` / `codegen.c` | Shared emit helpers, C type mappings, ARC scope tracking |
| `codegen_expr.c` | Expression and statement code generation, control flow, function emission |
| `codegen_types.c` | Struct/class/tuple/object literal type layouts, extern declarations |
| `zinc_runtime.h` | Self-contained runtime library (types + functions); copied to output directory alongside generated code |

### Compiler Modes

| Flag | Behavior |
|------|----------|
| `--ast` | Parse only, print AST |
| `--check` | Parse + semantic analysis, no codegen |
| *(no flags)* | Parse → analyze → codegen (emit `.c`/`.h` to stdout, don't compile) |
| `-c` | Full pipeline: parse → analyze → codegen → compile with GCC to executable |

## Key Implementation Patterns

- **Expression-oriented codegen:** GCC statement expressions `({...})` let control flow (`if`, `while`, `for`) appear in value positions.
- **ARC:** Strings, classes, arrays, and hashes are reference-counted via a `_rc` field. Retain/release calls are emitted at scope boundaries. Fresh allocations (tracked by `is_fresh_alloc`) skip the initial retain.
- **Callback-based collection ARC:** Arrays and hashes store function pointers for element retain/release, making the runtime generic over element type.
- **Parse-time desugaring:** `unless`→`if !`, `until`→`while !`, string interpolation→binary concat tree. These never reach semantic analysis or codegen as their original forms.
- **File-based runtime:** `zinc_runtime.h` is self-contained (type definitions + static functions). The compiler copies it to the output directory; generated `.h` files include it via `#include "zinc_runtime.h"`.

## Design Decisions

- **`print`** takes exactly 1 `String` argument, no auto-newline. Use `\n` and string interpolation for formatting.
- **No return type annotations** — function return types are always inferred.
- **No semicolons** — newlines are statement separators.
- **`unless`/`until`** are desugared at parse time, not in semantic analysis or codegen.
- **Struct vs Class:** Struct = value type (copied), Class = reference type (ARC). Both use `NODE_TYPE_DEF` with an `is_class` flag.
- **Weak references** are only allowed on class-typed fields within class definitions.
- **Not yet implemented:** closures, for-in loops.

## How to Add a New Feature

1. **Scanner** (`scanner.l`): Add tokens/keywords if needed
2. **Parser** (`parser.y`): Add grammar rules, create AST nodes
3. **AST** (`ast.h`/`ast.c`): Add node type enum, constructor, AST printer case
4. **Semantic** (`semantic.c`): Add type checking in `analyze_expr`/`analyze_stmt`, type inference in `get_expr_type`
5. **Codegen** (`codegen_expr.c` or `codegen_types.c`): Add C emission
6. **Runtime** (`zinc_runtime.h`): Add runtime support functions if needed
7. **Tests**: Add `test/pass/*.zn` (must exit 0) and `test/fail/*.zn` (with `# ERRORS: N` on line 1)
8. **README**: Document the feature with description and examples
9. **Verify**: `make clean && make && make test-all`

### Test Harness Details

- **`make test`** runs two suites: pass tests (`--ast` mode, expect exit 0) and fail tests (`--check` mode, counts parse+semantic errors, compares to `# ERRORS: N` annotation on line 1).
- **`make test-transpile`** runs `-c` mode on each pass test, then executes the resulting binary (expect exit 0).
- **`make test-all`** = both of the above.
- **Fail test annotation:** `# ERRORS: N` must be on **line 1**. The count includes both parse and semantic errors.
- **Transpiler test output** goes to `/tmp/zinc-test/` (cleaned up after the run).

## Conventions

- **Tests**: Pass tests are complete programs with a `main()` returning `0`. Fail tests use `# ERRORS: N` on line 1.
- **Leak tests**: Run `make test-leaks` after any ARC codegen change. Leak tests catch paths that pass tests miss.
- **Commits**: Short summary line describing the "what" and "why". Use imperative mood. If a fix targets the most recent commit, amend it (`git commit --amend`) rather than creating a separate fix commit.
- **Extern test symbols**: Use `zn_test_` prefixed names to avoid clashing with libc during transpile tests.

## Related Documents

| Document | Read when... |
|----------|-------------|
| `VALIDATION.md` | Asked to validate, or checking project correctness |
| `PLANNING.md` | Starting any non-trivial implementation or planning session |
| `REBUILD.md` | A history rewrite is needed |
