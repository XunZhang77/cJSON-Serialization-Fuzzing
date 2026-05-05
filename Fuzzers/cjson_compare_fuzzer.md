# `cjson_compare_fuzzer.c`

## Purpose

This harness exercises `cJSON_Compare` with explicit semantic oracles.

The original compare harness mostly:

- split fuzz input into two strings
- parsed both sides
- called `cJSON_Compare`

That was useful for reachability, but it did not prove much about correctness.

This revised harness adds:

- deterministic seeded trees
- targeted compare scenarios with known expected outcomes
- explicit strict-vs-loose key-matching checks
- reflexivity and symmetry checks
- duplicate-stability checks
- safe print/parse round-trip checks
- invalid-input and invalid-type checks
- a supplemental arbitrary parsed-pair path

The result is a semantic harness rather than a pure API exerciser.

## High-Level Strategy

The harness has two layers.

### 1. Structured compare scenarios

The first input byte controls how many scenario executions run.

Each scenario constructs one or two cJSON values with a known relation and then
asserts what `cJSON_Compare` must return for:

- strict mode: `case_sensitive = 1`
- loose mode: `case_sensitive = 0`

These scenarios cover the core semantics implemented in `cJSON_Compare`.

### 2. Supplemental parsed-pair path

After the structured loop, the harness reuses the remaining payload bytes as a
split text input for two parsed JSON values.

If parsing succeeds, the harness does not guess whether the trees should be
equal. Instead it checks stable relational properties:

- reflexivity
- symmetry
- duplicate stability
- round-trip stability
- `NULL` and invalid-type behavior

This preserves some arbitrary-input exploration from the original harness
without weakening the oracle quality.

## Core Helpers

### `fuzz_assert`

Aborts on violated invariants.

This makes semantic failures visible to libFuzzer and OSS-Fuzz in the same way
that memory safety failures are visible.

### `read_u8`, `read_small_int`, `read_bounded_string`

These helpers decode bounded control values from the fuzz stream.

They keep scenario behavior compact and deterministic:

- `read_u8` consumes one byte
- `read_small_int` maps one byte into `[-100, 100]`
- `read_bounded_string` produces a bounded heap string with embedded NULs
  normalized to `'A'`

### `find_split`

Used by the supplemental parsed-pair path.

It prefers `|` or newline as a separator and otherwise falls back to a midpoint
split.

## Seeded Tree

### `create_seed_tree`

This builds a deterministic serializable tree:

- `number` -> `1`
- `text` -> `"seed"`
- `flag` -> `true`
- `nested` -> object with:
  - `value` -> `7`
  - `label` -> `"alpha"`
- `meta` -> object with:
  - `kind` -> `"seed"`
  - `ready` -> `false`
- `items` -> array:
  - `1`
  - `"x"`
  - `false`

### Why a seeded tree matters

It lets the fuzzer revisit meaningful relational cases instead of only comparing
two unrelated parse results.

That matters because `cJSON_Compare` has different semantics for:

- objects
- arrays
- scalars
- key case sensitivity
- subsets
- ordering

The seeded tree gives the harness stable structure to mutate.

## Semantic Oracles

## 1. Reflexivity

For any valid cJSON value:

- `cJSON_Compare(x, x, 1)` must be true
- `cJSON_Compare(x, x, 0)` must be true

This is enforced by `verify_reflexive`.

## 2. Symmetry

For any pair of valid values:

- `Compare(a, b, mode)` must equal `Compare(b, a, mode)`

This is enforced by `verify_pair_relation` for both strict and loose mode.

## 3. Duplicate stability

For any valid value:

- `dup = cJSON_Duplicate(x, 1)`
- `Compare(x, dup, 1)` must be true
- `Compare(x, dup, 0)` must be true

For any structured scenario pair:

- duplicating both sides must preserve the expected compare result

This catches regressions where `cJSON_Compare` depends on pointer identity or
transient internal structure instead of semantic value.

## 4. Print/parse round-trip stability

When a tree is safe to round-trip:

1. print with `cJSON_PrintUnformatted`
2. parse the printed string
3. compare original and reparsed trees
4. print the reparsed tree again
5. require identical unformatted output

This is enforced by `verify_roundtrip_if_safe`.

The harness skips this oracle when the tree contains:

- raw nodes
- non-finite numbers

Those values are valid compare targets but are not stable under JSON text
round-tripping.

## 5. `NULL` and invalid-type behavior

The harness explicitly checks:

- `cJSON_Compare(NULL, NULL, mode)` -> false
- `cJSON_Compare(NULL, x, mode)` -> false
- `cJSON_Compare(x, NULL, mode)` -> false

It also builds intentionally invalid stack objects and requires compare to
reject them.

This follows the project’s own compare tests.

## Structured Scenario Set

The harness dispatches `opcode % 9`.

### Scenario 0: Seed equality

Build the seed tree and a recursive duplicate.

Expected:

- strict -> true
- loose -> true

### Scenario 1: Same numeric mutation

Mutate `nested.value` on both sides to the same bounded integer.

Expected:

- strict -> true
- loose -> true

This checks that equal numeric updates preserve semantic equality.

### Scenario 2: Different numeric mutation

Mutate `nested.value` on each side to different bounded integers.

Expected:

- strict -> false
- loose -> false

This checks that number inequality is observed.

### Scenario 3: Same string mutation

Mutate `nested.label` on both sides to the same fuzz-derived bounded string.

Expected:

- strict -> true
- loose -> true

### Scenario 4: Different string mutation

Mutate `nested.label` on each side to different strings.

Expected:

- strict -> false
- loose -> false

### Scenario 5: Case-sensitivity split

Construct objects that differ only in key case:

- left has `"False"`
- right has `"false"`

Expected:

- strict -> false
- loose -> true

This is one of the most important compare-specific oracles because it exercises
the actual `case_sensitive` parameter.

### Scenario 6: Object order independence

Construct two objects with the same members inserted in different orders.

Expected:

- strict -> true
- loose -> true

This checks that object comparison is key-based, not insertion-order-based.

### Scenario 7: Array order sensitivity

Construct two arrays with the same elements in different order.

Expected:

- strict -> false
- loose -> false

This checks that array comparison is positional.

### Scenario 8: Subset or raw comparison

This opcode family splits by the opcode low bit.

#### Even opcodes: subset inequality

Start from two equal seeded trees, then remove `meta` from the right-hand side.

Expected:

- strict -> false
- loose -> false

This checks that one object being a subset of another is not considered equal.

#### Odd opcodes: raw-literal comparison

Create raw nodes from a small set of valid JSON literals:

- `null`
- `true`
- `false`
- `0`
- `42`
- `"raw"`
- `[]`
- `{}`

Expected:

- same literal string -> true
- different literal string -> false

This targets the `cJSON_Raw` compare branch directly.

## Supplemental Parsed-Pair Path

### `run_parsed_pair_scenario`

After the structured loop, the harness interprets the post-header payload as
two candidate JSON texts.

If parsing succeeds on one or both sides, it checks:

- reflexivity of each parsed value
- duplicate equality of each parsed value
- print/parse round-trip equality of each parsed value
- `NULL` and invalid compare behavior
- symmetry of pairwise compare results
- duplicate stability of the pairwise result

This path keeps arbitrary parser-generated shapes in play while still requiring
concrete correctness properties.

## Why these oracles are meaningful

The harness is designed around the actual semantics in `cJSON_Compare`:

- identical scalars must compare equal
- different scalars of the same kind may compare unequal
- objects compare by key/value relation
- array order matters
- object member order does not
- case sensitivity changes key matching rules
- subsets are not equal
- invalid or `NULL` inputs are rejected

The oracle set matches those semantics directly rather than inferring behavior
from crashes or from shallow structural checks alone.

## Input Layout

## Header

The first byte controls the structured scenario count:

- `case_count = raw_cases % 24`

## Structured body

Each iteration consumes:

- one opcode byte
- additional bytes required by the chosen scenario

Examples:

- numeric scenarios consume one or two bounded integer bytes
- string scenarios consume bounded-string encodings
- raw comparison consumes two selector bytes

## Supplemental parsed payload

The same post-header payload is also reused by the parsed-pair path.

This means a single input can contribute to:

- structured semantic scenarios
- arbitrary parsed-tree invariants

## Ownership and Cleanup

Every structured scenario uses fresh trees and deletes them before returning.

Ownership rules are simple:

- seeded builders own their complete returned trees
- `compare_pair` owns `left` and `right`
- `delete_pair` frees both sides
- temporary duplicates are freed immediately after verification
- temporary parse buffers and printed strings are always freed

There are no intentional shared-reference ownership transfers in this harness.

## Enforced Invariants Summary

The harness enforces the following correctness statements:

- valid values compare equal to themselves
- compare is symmetric
- duplicate trees preserve equality
- safe trees survive print/parse round-trips semantically and textually
- `NULL` inputs are rejected
- invalid cJSON type combinations are rejected
- equal seeded trees compare equal
- equal mutations on both sides preserve equality
- unequal mutations break equality
- key-case-only differences are strict-false and loose-true
- object insertion order does not affect equality
- array element order does affect equality
- object subsets are not equal
- raw nodes compare by raw string value

## Improvement Over The Original Compare Harness

Compared to the original compare harness, this version adds:

- deterministic relational test cases
- explicit expected outcomes
- direct coverage of strict-vs-loose semantics
- subset and ordering oracles
- raw-node compare coverage
- duplicate and round-trip validation
- invalid-input assertions
- documentation of the harness model and invariants

That makes it a substantially stronger OSS-Fuzz target for correctness, not
just crash discovery.
