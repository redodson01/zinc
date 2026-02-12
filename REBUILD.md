# Zinc — History Rebuild

Read this document when a history rewrite is needed. For the general planning process, see `PLANNING.md` first.

## When to rebuild

Rebuild when a change needs to be folded into an earlier commit to maintain a clean history (see claim 4 in `VALIDATION.md`). If the change can go at HEAD as a new commit without violating history cleanliness, prefer that — it's simpler and less error-prone.

## Pre-flight (in addition to PLANNING.md checklist)

1. `git tag <backup> HEAD` — tag the current committed state.
2. Copy modified files to `/tmp/zinc-rebuild/` as a safety net.
3. Ensure the plan file includes the full rebuild procedure and per-commit edits.
4. Any rebuild script must check `git status --porcelain` at startup and abort if dirty.
5. Only then create an orphan branch or start cherry-picking.

## Approaches

Choose based on scope:

**Cherry-pick with amendments** — Small surgical changes to a few commits. Cherry-pick unchanged commits directly; cherry-pick then amend modified ones. Use when most commits pass through untouched.

**Full branch rebuild** — Large restructures (reorder, split, merge commits) without new code. Assemble each commit on an orphan branch by checking out file states from source commits.

**Two-phase rebuild** — Code changes + history restructuring. This is the most common case.
- **Phase 1:** Implement all code changes on HEAD. Edit, build, test, iterate freely. No git history changes. Goal: a working final state with all tests passing.
- **Phase 2:** Rebuild history on an orphan branch. Distribute Phase 1's changes to the commits where their features are introduced. This phase is purely mechanical — no new code, just applying known-good diffs.
- **Handoff:** Between phases, document the exact diffs from Phase 1 in the plan file. Phase 2 should be executable from the plan file alone, without needing to re-derive any changes.

## Rebuild procedure

All approaches follow four phases:

1. **Plan** — Write the complete plan file. No git commands until it's written.
2. **Rebuild** — Create orphan branch (`git checkout --orphan rebuild && git rm -rf .`). For each commit: cherry-pick or checkout source → apply per-commit edits → build and test → commit. Test every 2-3 commits at minimum.
3. **Verify** — Run the verification checklist BEFORE replacing the old branch.
4. **Replace** — `git checkout main && git reset --hard rebuild && git branch -d rebuild`. Keep the backup tag until the remote push succeeds.

## Per-commit construction

Each commit must contain only code relevant to features that exist at that point — no dead code, no forward references. This requires:

- **Fix-to-commit mapping:** Which change lands at which commit, with cross-file dependencies noted.
- **Variant file enumeration:** Not just `src/` files — also `zinc_runtime.h` (embedded in output), `parser.y` (calls AST constructors), `Makefile` (references source files). Ask: "does this file's HEAD version work at commit 2?"
- **Python scripts for mechanical edits:** More reliable than manual edits. Parameterize by commit number/features. Test on sample files from 2-3 source commits first.
- **Shell script for the rebuild cycle:** Automates: checkout source → run edit scripts → build → test → commit.

## Verification checklist template

Run these before replacing the old branch:

| Check | Command |
|-------|---------|
| Commit count | `git log --oneline \| wc -l` |
| Messages match | `git log --oneline` |
| Per-commit content | `git show <hash>:<path>` for each modified commit |
| Test counts | `ls test/pass/*.zn \| wc -l` and `ls test/fail/*.zn \| wc -l` |
| All tests pass | `make clean && make && make test-all` |
| Source matches expected | `git diff <backup> -- src/` |
| No formatting regressions | Review diff for indentation issues (C compiles with wrong indent) |
| Range diff | `git range-diff origin/main...main` |

Use `git show <hash>:<path>`, not `git log -p`, for per-commit verification.

## Hard-won lessons

- **"Compiler builds" does not mean "generated code works."** Always run transpiler tests. The compiler may emit C that references runtime functions not yet defined at that commit.
- **Smoke-test at the earliest commit first.** Build and run a transpiler test at commit 2 before writing variants for all commits.
- **Build linearly, not in parallel.** Commit N → test → commit N+1. Generating all variants at once creates big-bang integration problems.
- **Test annotations cascade.** A code change that reduces semantic errors will break fail test `# ERRORS: N` annotations. Discover this in Phase 1, not mid-rebuild.
- **Data structures grow across commits.** The `Type` struct, `TypeKind` enum, and runtime types gain fields/values as features land. Edit scripts must be parameterized by commit number.
- **Use brace-counting, not regex, for multi-line C replacement.** Regex with `re.DOTALL` doesn't respect nesting depth. Write a helper that counts `{`/`}` depth and handles if/else chains.
- **Diff against reference after every commit, not just at the end.** Run `git diff <backup> -- src/` after each commit to catch drift immediately. Cosmetic divergences (comment wording, function ordering, variable names) accumulate silently across commits and are harder to fix in bulk.
- **Tests only catch what they exercise.** A missing codegen branch (e.g., TK_CLASS in if-expressions) can pass all existing tests if no test hits that path. Run `make test-leaks` after any ARC codegen change — leak tests stress paths that pass tests don't.
