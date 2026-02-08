# Zinc — Project Guide

## Build & Test

```bash
make            # Build build/zinc
make test-all   # Run all tests (pass + fail + transpile)
make clean      # Clean build artifacts
```

## Validation Workflow

Three claims must always hold. When asked to validate, check each and fix any gaps.

### 1. All documented features are fully implemented

Every feature described in the README has working support across the full pipeline: parsing (scanner.l, parser.y), semantic analysis (semantic.c), and code generation (codegen.c, codegen_expr.c, codegen_types.c).

### 2. Full test coverage

- **Pass tests** (`test/pass/`): Every language feature has at least one passing test that exercises parsing, semantic analysis, and transpilation.
- **Fail tests** (`test/fail/`): Every semantic error the analyzer can produce is exercised by a fail test. Each fail test has a `# ERRORS: N` annotation on line 1.
- Verify with `make test-all` — all tests must pass with 0 failures.

### 3. Accurate README documentation

Every implemented feature is documented in README.md with:
- A description of the feature
- Code examples showing usage
- Coverage of relevant syntax variants (e.g., type annotations in function signatures, not just literals)

The README should reflect what the language *actually supports*, no more and no less.

## Planning

### Evaluate alternatives before committing to a design

Every plan must include an **Alternatives Considered** section that lists at least two approaches with trade-offs before selecting one. The section should address:

- How do other languages / established systems solve this problem?
- What is the simplest approach that works?
- What are the runtime costs, implementation complexity, and user-facing implications of each?

Do not skip this step even when a solution seems obvious. Over-engineered solutions often start as "obvious" ones that weren't compared against simpler alternatives.

### Never silently deviate from an approved plan

If a significant portion of an approved plan cannot be executed — whether due to lost work, unexpected complexity, or changed assumptions — **stop and discuss with the user** before proceeding. Do not skip sections and note the deviation in a file. The user approved a specific scope; changing that scope requires explicit approval.

### Commit before branch operations

Before creating branches, switching branches, or starting history rewrites, ensure all working tree changes are committed — even as a temporary WIP commit that will be amended later. Uncommitted work is destroyed by branch operations.

## History Rewrite Process

When the commit history needs cleaning up, follow this process. The goal is a history where each commit is a complete, meaningful, iterative change — telling a readable story of how the project evolved. Someone new to the repo should be able to read the log and understand the progression.

### Goals for a clean history

- Each commit introduces **one coherent change** (a feature, a fix, a refactor — not a mix).
- Each commit **builds and passes tests** at that point in the sequence.
- Each commit's README **fully documents all language features that exist at that point** — documentation is introduced alongside the feature, not retroactively in a later commit.
- Later commits don't retroactively fix or reverse earlier ones — fold those corrections into the original commit instead.
- The sequence is **logically ordered**: foundational work first, features that depend on it after.
- Commit messages are clear and accurately describe what changed.

### Choosing an approach

Automated `git rebase -i` with reordering/squashing fails in practice when later commits revise earlier design decisions, because it creates cascading patch conflicts. Three approaches work; choose based on scope.

#### 1. Cherry-pick with amendments

**When:** Small surgical changes — folding fixes into a few specific commits while the rest stay identical.

Cherry-pick unchanged commits directly; for modified commits, cherry-pick then amend.

#### 2. Full branch rebuild (history-only)

**When:** Large restructures that reorder, split, or merge commits, but don't involve code changes beyond what's already in the source history.

Create a new orphan branch and assemble each target commit by checking out file states from source commits. This is conflict-free because we're writing complete file contents, not applying patches.

#### 3. Two-phase rebuild (code changes + history)

**When:** The rebuild involves both **new code changes** (refactors, new features, bug fixes) AND **history restructuring**. This is the most complex case but the most common for significant cleanups.

**Phase 1 — Implement changes on HEAD.** Make all code changes against the current HEAD, where you have a single known-good codebase and the full test suite. Iterate freely: edit, build, test, repeat. Do not touch git history during this phase. The goal is a working final state with all tests passing.

**Phase 2 — Rebuild history.** Create an orphan branch and assemble each target commit, incorporating the code changes from Phase 1 at the appropriate points.

**Why two phases work better than one:**

- **Decouples correctness from history.** You can focus on "do these changes work?" without git complexity. Fixing a bug is just edit-build-test, not amend-and-replay-forward.
- **Changes inform each other.** Implementing change A on HEAD may reveal that it affects change B (e.g., a new semantic analyzer produces different error counts, requiring test annotation updates). You discover this before the rebuild, not as a confusing mid-rebuild surprise.
- **Enables the overlay approach.** If the code changes are backward-compatible (unused handlers compile as dead code), you can overlay HEAD's modified files onto each source commit during Phase 2, making the rebuild largely mechanical.

**Tradeoffs:**

- Some work is "duplicated" — you build the final state in Phase 1, then distribute it across commits in Phase 2. Files that need per-commit variants (e.g., a runtime header that gains features incrementally) must be constructed for each commit.
- Uses roughly double the context. For very large rebuilds, expect to span multiple sessions.

### Phases (all approaches)

All approaches follow the same four phases:

1. **Plan phase** — Analyze the current history and design the target commit sequence. Write the plan to a file (see below). **No git commands until the plan file is written.**
2. **Rebuild phase** — Create a new branch and build the target history.
3. **Verify phase** — Run the verification checklist from the plan file **before** replacing the old branch, while there's still an opportunity to fix gaps cheaply.
4. **Replace phase** — Once verified, replace the old branch with the new one.

### The overlay approach

When the code changes from Phase 1 are backward-compatible — i.e., the modified files compile even when some features don't exist yet — you can **overlay** HEAD's files onto each source commit rather than creating unique intermediate versions.

**How it works:** For each commit, start from the source commit's files, then replace the architectural files (e.g., ast.h, semantic.c, codegen*.c) with HEAD's versions. Handlers for node types that don't exist yet are dead code but compile fine.

**What still needs per-commit attention:**

- **Files that gain features incrementally.** A runtime header that defines array functions can't be overlaid onto a commit that predates arrays. Identify these files during planning and pre-generate the needed variants (see plan file section).
- **Test annotations.** If the new code changes error counts (e.g., fewer cascading errors from a smarter analyzer), fail test `# ERRORS: N` annotations may need updating at each commit.
- **Parser changes.** New syntax rules must only appear in commits where the feature exists.
- **README content.** Each commit's README should document exactly the features present at that point. When a later commit adds content to the same section, carry forward the earlier commit's exact wording — don't rephrase it. Inconsistent wording across commits makes it look like the text was written after the fact rather than alongside the feature.

### The plan file

Before starting any rebuild — no matter how small — write the complete plan to a scratchpad file. This is non-negotiable; context can compact at any time and the plan file is the only thing that survives.

**Location**: Write to `rebuild-plan.md` in the auto memory directory (`~/.claude/projects/.../memory/`). This persists across sessions and is outside the repository.

The plan file must contain:

- **Source history**: summary of existing commits (hashes, descriptions, what they touch).
- **Target history**: the desired commit sequence, numbered, with:
  - Commit message
  - What the commit should contain (which files, what state)
  - Which source commit(s) to draw from
  - What differs from the source commit, if anything
- **Per-commit delta table**: For every commit that differs from its source, list the **specific fixups** required. Not just "overlay HEAD files + fix annotations" — spell out exactly:
  - Which files to overlay vs. keep from source
  - Which file variants to use (e.g., "runtime: strings-only version", "runtime: arrays version")
  - Exact test annotation changes (e.g., "`undefined_variable.zn`: `# ERRORS: 3` → `# ERRORS: 2`")
  - Which parser rules to add/remove
  - Which README sections to include
- **Generated content**: Full contents of any files that need per-commit variants — not just descriptions, but the actual file content so it can be recreated after compaction. This includes:
  - New test files
  - Runtime header variants (if the runtime gains features across commits)
  - Any config or build file variants
- **Verification checklist**: concrete, runnable checks — one per line, each with a specific command and expected output (see template below).
- **Current progress**: which step we're on (update as we go).

At any point, re-read the plan file to recover full context. It is the source of truth.

#### Standard verification checks

Every rebuild verification checklist should include at minimum:

| Check | How |
|-------|-----|
| Commit count matches target | `git log --oneline \| wc -l` |
| Commit messages match target | `git log --oneline` |
| Per-commit content changes are present | `git show <hash>:<path>` for each modified commit |
| Pass test count unchanged (or expected) | `ls test/pass/*.zn \| wc -l` |
| Fail test count unchanged (or expected) | `ls test/fail/*.zn \| wc -l` |
| All tests pass | `make test-all` |
| Final tree matches expected file set | `diff` of `git ls-tree -r --name-only` between rebuild and expected |
| Source files identical to original | `git diff <backup>:src/ HEAD:src/` is empty |

Add project-specific checks as needed (e.g., verifying specific content was folded into a specific commit).

### Caveats

- **Always write the plan to a scratchpad file first**: Before running any git command, write the complete plan — including the verification checklist, per-commit delta table, and any generated content (file variants, test files) — to a file. No exceptions, regardless of how small the rebuild seems. Context can compact at any time.
- **Pre-generate all file variants**: If a file needs different content at different commits (e.g., a runtime header that gains features), write all variants into the plan file upfront. Do not construct them on the fly during the rebuild — you may lose context between commits.
- **No `-i` flag**: `git rebase -i` and `git add -i` require interactive input and don't work in this environment.
- **Always preserve a backup**: Tag or branch the original HEAD before starting so nothing is lost.
- **Test incrementally**: Build and test every 2-3 commits during the rebuild. Don't wait until the end to discover a commit doesn't build. Earlier is cheaper.
- **Watch for cascading effects**: A code change that seems local may have surprising downstream effects. For example, a smarter semantic analyzer may produce fewer errors, breaking fail test annotations across the entire history. Discover these in Phase 1 (if using two-phase), and enumerate every affected file in the plan.
- **Watch for file deletions**: When a target commit removes a file that existed in a prior commit, make sure the deletion is explicit — don't just omit the file.
- **Context compaction**: If context gets compacted mid-rebuild, re-read the plan file and the current state of the rebuild branch to pick up where we left off.
- **Cross-reference subagent output**: Subagents don't have prior context. Before trusting a subagent's verification, re-read the plan file yourself and confirm the subagent checked the right items.

## Conventions

- **Tests**: Pass tests are complete programs with a `main()` returning `0`. Fail tests use `# ERRORS: N` on line 1.
- **Commits**: Short summary line describing the "what" and "why". Use imperative mood.
- **Extern test symbols**: Use `zn_test_` prefixed names to avoid clashing with libc during transpile tests.
