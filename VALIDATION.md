# Zinc — Validation

Four claims define correctness for the Zinc project. All four must hold at all times. When asked to validate, check each and fix any gaps.

## 1. All documented features are fully implemented

Every feature described in the README has working support across the full compilation pipeline: scanning (`scanner.l`), parsing (`parser.y`), AST construction (`ast.h`/`ast.c`), semantic analysis (`semantic.c`), and code generation (`codegen.c`, `codegen_expr.c`, `codegen_types.c`).

**Verify:** For each README section, confirm that the described syntax parses, type-checks, and transpiles to working C. Cross-reference against pass tests — every feature should have at least one test that exercises it end-to-end.

**If it fails:** Implement the missing pipeline stage. A feature is not complete until it works through `-c` mode (transpile + compile + run).

## 2. Full test coverage

- **Pass tests** (`test/pass/`): Every language feature has at least one test that exercises parsing, semantic analysis, and transpilation. Tests are complete programs with `main()` returning `0`.
- **Fail tests** (`test/fail/`): Every semantic error the analyzer can produce is exercised by a fail test. Each fail test has `# ERRORS: N` on line 1 (counts both parse and semantic errors).
- **Transpiler tests**: Every pass test also passes transpilation (`-c` mode → compile → run → exit 0).

**Verify:**
```bash
make clean && make && make test-all
```

Expected output (current counts):
- 26 pass tests, 43 fail tests → `Test Summary: 69 passed, 0 failed`
- 26 transpiler tests → `Transpiler Summary: 26 passed, 0 failed`
- 35 leak tests → `Leak Test Summary: 35 passed, 0 failed`

**If it fails:** Add missing tests or fix the code. Never adjust `# ERRORS: N` annotations without understanding why the count changed.

## 3. Accurate README documentation

Every implemented feature is documented in `README.md` with:
- A description of the feature
- Code examples showing usage
- Coverage of relevant syntax variants (e.g., type annotations in function params, not just literals)

The README reflects what the language *actually supports* — no more, no less.

**Verify:** Read the README and cross-reference against the implementation. Check for features that are implemented but undocumented, or documented but unimplemented.

**If it fails:** Update the README to match reality. Do not document planned features as if they exist.

## 4. Clean commit history

Each commit in the history must satisfy:
- Introduces **one coherent change** (a feature, a fix, or a refactor — not a mix)
- **Builds and passes all tests** at that point in the sequence
- Its **README documents exactly the features that exist** at that commit
- Does not retroactively fix or reverse an earlier commit
- Is **logically ordered**: foundational work before dependent features
- Has a **clear commit message** in imperative mood

**Verify:**
```bash
git log --oneline                    # Check messages and count
git range-diff origin/main...main    # Compare with remote
```

Spot-check intermediate commits:
```bash
git show <hash>:README.md | head -20   # README matches features at that point
git stash && git checkout <hash> && make clean && make && make test-all   # Builds and passes
```

**If it fails:** See `REBUILD.md` for the history rewrite process.
