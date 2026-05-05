# `cjson_duplicate_fuzzer.c`

## Purpose

This harness exercises `cJSON_Duplicate` with explicit semantic oracles.

The original duplicate harness mostly:

- parsed fuzz input
- called `cJSON_Duplicate`
- printed the duplicate

That reached the API, but it did not prove much about correctness.

This revised harness checks the actual duplicate contract:

- `cJSON_Duplicate(NULL, recurse)` must return `NULL`
- top-level duplicates must be detached (`next == NULL`, `prev == NULL`)
- duplicates must strip `cJSON_IsReference`
- recursive duplicates must preserve semantic equality
- non-recursive duplicates must drop children
- duplicated trees must be structurally independent from the source
- mutating a duplicate must not mutate the original
- recursive duplication must reject circular structures

It also keeps a parsed-input path so the fuzzer still explores arbitrary JSON
trees in addition to the deterministic scenarios.

## High-Level Structure

The harness has two layers.

### 1. Structured scenarios

The first input byte determines how many scenario executions run.

Each scenario constructs a known tree or value and then checks one specific
property of duplication:

- recursive deep-copy behavior
- non-recursive shallow-copy behavior
- scalar string duplication
- string-reference duplication
- constant-key duplication
- circular-reference failure

### 2. Supplemental parsed-input path

After the structured loop, the remaining payload is interpreted as a single
candidate JSON text.

If parsing succeeds, the harness duplicates that parsed tree with both:

- `recurse = 0`
- `recurse = 1`

It then applies the generic duplicate oracles that are safe for arbitrary parse
trees.

## Core Helpers

### `fuzz_assert`

Aborts on violated invariants.

This turns semantic duplicate regressions into fuzzer-visible failures.

### `read_u8`, `read_small_int`, `read_bounded_string`

These helpers decode bounded values from the fuzz stream.

They are only used to steer scenario count and mutation payloads; they do not
define the correctness logic.

### `tree_has_ambiguous_object_keys`

This detects objects containing duplicate keys that are equal under
case-insensitive matching.

`cJSON_Compare` is not stable enough to use as a semantic oracle for those
trees, so the harness skips compare-based equality checks for them while still
checking duplicate metadata and pointer independence.

## Seed Tree

### `create_seed_tree`

The main deterministic object contains:

- `number` -> `1`
- `text` -> `"seed"`
- `flag` -> `true`
- `nested` -> object:
  - `value` -> `7`
  - `label` -> `"alpha"`
- `items` -> array:
  - `1`
  - `"x"`
  - `false`
- `constKey` -> number inserted with `cJSON_AddItemToObjectCS`

This shape is chosen because it simultaneously covers:

- scalar values
- nested objects
- arrays
- heap-owned strings
- constant object keys

## Duplicate Oracles

## 1. `NULL` input behavior

The harness always checks:

- `cJSON_Duplicate(NULL, 0) == NULL`
- `cJSON_Duplicate(NULL, 1) == NULL`

This matches the library contract and project tests.

## 2. Top-level duplicate metadata

For every successful duplicate, the harness checks:

- duplicate pointer differs from source pointer
- top-level `next == NULL`
- top-level `prev == NULL`
- `cJSON_IsReference` is cleared
- base JSON type is preserved

It also checks string/value ownership semantics:

- `valuestring` content is preserved
- duplicated `valuestring` storage is distinct
- constant key strings preserve `cJSON_StringIsConst`
- constant keys may intentionally keep the same key pointer
- non-const keys must be duplicated into distinct storage

These checks are implemented in
`verify_top_level_duplicate_metadata`.

## 3. Deep structural independence

For recursive duplicates, the harness traverses the original and duplicate in
parallel and checks:

- corresponding nodes are different pointers
- corresponding child chains are different pointers
- string/value contents are preserved
- key-pointer behavior matches the const/non-const rule
- duplicated nodes do not retain `cJSON_IsReference`

This is implemented in `verify_tree_distinct`.

This oracle is stronger than plain `cJSON_Compare` because it proves the
duplicate is actually a copy, not just semantically equal text.

## 4. Safe print/parse round-trip

When a tree has no ambiguous object keys, the harness checks:

1. print with `cJSON_PrintUnformatted`
2. parse the printed text
3. require semantic equality with the printed source tree
4. print the reparsed tree again
5. require identical unformatted output

This is implemented in `verify_roundtrip_if_safe`.

## 5. Mutation independence

The harness mutates duplicates after creation and requires the source tree to
remain unchanged.

Mutation targets are:

- numbers via `cJSON_SetNumberValue`
- strings via `cJSON_SetValuestring`

This is done both in deterministic scenarios and in the parsed-input path.

If the source and duplicate were still aliased, these checks would fail.

## Structured Scenario Set

The structured loop dispatches `opcode % 5`.

### Scenario 0: Recursive seed-tree duplicate

Flow:

1. build the seed tree
2. duplicate with `recurse = 1`
3. check metadata
4. check deep pointer independence
5. require semantic equality with `cJSON_Compare`
6. round-trip both trees through print/parse
7. mutate the duplicate
8. require the original tree to stay unchanged
9. duplicate the original again
10. mutate the original
11. require the second duplicate to stay unchanged

This is the main correctness scenario for deep-copy behavior.

### Scenario 1: Non-recursive seed-tree duplicate

Flow:

1. build the seed tree
2. duplicate with `recurse = 0`
3. require `dup->child == NULL`
4. require zero child count
5. require the duplicate to print as `{}`
6. require the original and duplicate not to compare equal

This proves that non-recursive duplication preserves only the top-level node
and intentionally drops the subtree.

### Scenario 2: Heap-owned string scalar

Flow:

1. build a `cJSON_String` from fuzz input
2. duplicate it
3. require equal content and distinct `valuestring` storage
4. mutate the duplicate string
5. require the original string to remain unchanged

This directly checks scalar-string duplication and independence.

### Scenario 3: String reference

Flow:

1. build a `cJSON_CreateStringReference(payload)`
2. duplicate it
3. require the duplicate to clear `cJSON_IsReference`
4. require equal string content
5. require the duplicate string storage to be newly owned
6. mutate the duplicate string
7. require the original referenced string to remain unchanged

This directly targets the `item->type & ~cJSON_IsReference` behavior in
`cJSON_Duplicate`.

### Scenario 4a: Constant-key object

When `opcode % 5 == 4` and the opcode low bit is even:

1. build an object whose child key is inserted with `cJSON_AddItemToObjectCS`
2. duplicate recursively
3. require both original and duplicate child keys to keep
   `cJSON_StringIsConst`
4. require the duplicate child node pointer to differ
5. require the constant key pointer to remain the same literal pointer
6. mutate the duplicate child value
7. require the original child value to remain unchanged

This covers the const-key branch inside duplicate.

### Scenario 4b: Circular-reference failure

When `opcode % 5 == 4` and the opcode low bit is odd:

1. build a circular array graph: `root -> a -> b -> root`
2. require `cJSON_Duplicate(root, 1) == NULL`
3. require `cJSON_Duplicate(root, 0) != NULL`
4. require the shallow duplicate to be an empty array
5. break the cycle
6. delete all objects cleanly

This explicitly targets the circular-limit failure path used by recursive
duplication.

## Supplemental Parsed-Input Path

### `run_parsed_duplicate_scenario`

After the structured loop, the remaining payload is copied into a NUL-terminated
buffer and parsed as JSON.

If parsing succeeds:

#### Shallow duplicate path

- duplicate with `recurse = 0`
- verify top-level metadata
- round-trip the duplicate through print/parse
- if the source had children:
  - require `dup->child == NULL`
  - for objects require printed text `{}`
  - for arrays require printed text `[]`
  - if compare is safe, require source and duplicate not to compare equal
- if the source had no children:
  - if compare is safe, require equality

#### Recursive duplicate path

- duplicate with `recurse = 1`
- verify top-level metadata
- verify deep structural independence
- round-trip the duplicate through print/parse
- if compare is safe, require equality with the source
- mutate the duplicate’s first reachable number or string
- require the source tree to remain unchanged
- if compare is safe, require source and mutated duplicate not to compare equal

This keeps arbitrary parsed shapes in scope while preserving meaningful
duplicate-specific oracles.

## Ownership Model

The harness is careful about ownership because duplicate correctness depends on
it.

### Normal values

For ordinary cJSON values, the tree owns its nodes and strings.

`cJSON_Delete(tree)` frees the full structure.

### String references

For `cJSON_CreateStringReference(payload)`:

- the cJSON node does not own `payload`
- the harness keeps `payload` alive until after the cJSON node is deleted
- the duplicate must allocate and own its own copy of that string

### Circular graph scenario

The circular scenario cannot be deleted directly while the cycle exists.

So the harness:

1. creates the cycle
2. checks recursive duplicate failure
3. detaches the back-edge with `cJSON_DetachItemFromArray`
4. deletes the now-acyclic graph

This mirrors the project’s own tests.

## Enforced Invariants Summary

The harness enforces these statements:

- `cJSON_Duplicate(NULL, recurse)` returns `NULL`
- successful duplicates are detached at top level
- duplicates preserve base type
- duplicates clear `cJSON_IsReference`
- recursive duplicates preserve semantic equality when compare is safe
- recursive duplicates allocate structurally distinct nodes
- non-recursive duplicates drop the subtree
- duplicated strings preserve content but not ownership aliasing
- constant keys preserve const-key semantics
- mutating a duplicate does not mutate the source
- mutating the source does not mutate an existing duplicate
- recursive duplication rejects circular structures
- shallow duplication of a circular container still succeeds

## Improvement Over The Original Duplicate Harness

Compared to the original harness, this version adds:

- deterministic structured scenarios
- explicit recursive vs non-recursive semantics
- deep-copy pointer-independence checks
- mutation-separation checks
- reference-bit stripping checks
- const-key behavior checks
- circular-reference failure checks
- a documented parsed-input duplicate path
- a README that explains the oracle model and its limits

That makes it a substantially better OSS-Fuzz harness for duplication
correctness rather than just duplication reachability.
