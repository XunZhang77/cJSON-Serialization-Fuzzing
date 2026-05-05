# `cjson_replace_item_in_object_fuzzer.c`

## Purpose

This harness exercises cJSON object replacement APIs:

- `cJSON_ReplaceItemInObject`
- `cJSON_ReplaceItemInObjectCaseSensitive`

It focuses on API-level correctness for replacing an existing object member and
for failing cleanly when the target key does not exist.

The current harness intentionally avoids deep semantic subtree comparison.
Earlier versions tried stronger compare-based oracles, but those proved too
fragile for fuzz-generated replacement values.

## Execution Flow

## 1. Entry point

`LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)` performs the full
harness lifecycle.

The function:

1. rejects `NULL` or empty input
2. creates a root object
3. seeds the root with a fixed set of members
4. consumes one byte for operation count
5. executes up to `raw_ops % 32` replace attempts
6. runs the final print/parse structural oracle
7. deletes the root

Unlike the delete harness, this one does not rebuild state during the loop.
Each operation therefore accumulates on the current root object.

## 2. Seeded JSON shape

`seed_object(root)` creates a flat object with six members:

- `alpha` -> number
- `Bravo` -> number
- `CHARLIE` -> number
- `delta` -> string
- `Echo` -> bool
- `foxtrot` -> null

There are no nested seeded objects in this harness. Nested structures instead
come from fuzz-generated replacement values.

## 3. Per-operation flow

Each loop iteration:

1. reads a key string
2. reads a case-mutation mode and transforms the key
3. builds a replacement cJSON subtree
4. reads an API selector
5. determines whether the target exists using matching lookup semantics
6. records object size
7. performs the replace call
8. validates API-level postconditions
9. frees the temporary key

## 4. Final oracle phase

After all operations:

1. the root is printed with `cJSON_PrintUnformatted`
2. the resulting string is reparsed
3. if parsing succeeds, the parsed tree is deleted
4. all remaining owned objects are freed

This final step preserves the original structural oracle:

- the final tree should remain printable
- the printed JSON should remain parseable

## Function-By-Function Documentation

## Input helpers

### `read_u8`

Reads one byte if available.

Behavior:

- returns `1` on success
- returns `0` if no byte remains
- advances `*data`
- decrements `*size`

### `read_bytes`

Reads exactly `n` bytes into an output buffer if enough data remains.

Behavior:

- returns `1` on success
- returns `0` if fewer than `n` bytes remain
- advances `*data`
- decrements `*size` by `n`

This helper is used by `read_double`.

### `read_double`

Constructs a `double` from up to eight fuzz bytes.

Behavior:

- if eight bytes are available, they are copied directly into a `uint64_t`
  backing store
- if fewer than eight bytes remain, the available prefix is copied and the rest
  stays zero
- the union field is then interpreted as `double`

This allows the harness to generate a wide range of numeric values, including
special floating-point values such as `NaN` and `Inf`.

## Key helpers

### `read_key`

Creates a fuzz-derived heap string.

Behavior:

- consumes one byte for the raw length
- bounds length to `raw_len % 64`
- truncates further if the remaining input is shorter
- allocates a NUL-terminated string
- replaces embedded NUL bytes with `'A'`

This key may or may not match one of the currently attached object members.

### `mutate_case`

Applies deterministic case mutation after key creation.

Supported modes after `% 4`:

- `0` -> unchanged
- `1` -> uppercase lowercase letters
- `2` -> mixed alternating transformation
- `3` -> lowercase uppercase letters

This is used to explore the semantic difference between case-sensitive and
non-case-sensitive replacement APIs.

## Oracle helper

### `lookup_object_item`

Performs the matching lookup corresponding to the replace API under test:

- non-case-sensitive -> `cJSON_GetObjectItem`
- case-sensitive -> `cJSON_GetObjectItemCaseSensitive`

The harness uses this helper both before and after replacement so that the
oracle matches cJSON's own lookup semantics.

## Replacement-value construction helpers

### `make_leaf_item`

Constructs a non-container replacement item based on `kind % 4`.

Possible outputs:

- number
- string
- bool
- null

The helper may consume additional bytes depending on the chosen type:

- numbers use `read_double`
- strings use `read_key`
- bools use one byte
- null uses no extra payload

### `make_replacement_item`

Constructs the subtree inserted by a successful replace operation.

It first reads one byte for `kind`.

If `depth <= 0`, it falls back to `make_leaf_item`.

Otherwise, `kind % 6` selects one of:

- four leaf-like cases
- one array case
- one object case

#### Array case

When `kind % 6 == 4`:

1. create an array
2. read one byte for element count
3. bound count to at most three elements via `% 4`
4. recursively generate each element at `depth - 1`

#### Object case

When `kind % 6 == 5`:

1. create an object
2. read one byte for member count
3. bound count to at most two members via `% 3`
4. repeatedly read keys and recursively create values at `depth - 1`

#### Failure behavior

If insertion into a new array or object fails:

- the just-created value is deleted
- the partially built container is deleted
- the function returns `NULL`

This prevents leaks on construction failure.

## Seed helper

### `seed_object`

Creates the deterministic initial object used for replacement testing.

The key names deliberately vary in case so that:

- exact-match success paths exist
- case-insensitive success paths exist
- case-sensitive miss paths exist

## Core replace-loop documentation

The main logic lives directly inside `LLVMFuzzerTestOneInput`.

For each operation, the harness maintains the following local state:

- `lookup_key` -> mutated lookup string
- `case_sensitive` -> selected API family
- `existing` -> whether the target exists under that API's lookup semantics
- `size_before` -> root object size before replacement
- `replace_ok` -> cJSON API return value
- `size_after` -> root object size after replacement
- `after_item` -> post-replace lookup result for the same key

This bookkeeping supports the current API-level oracle set.

## Oracle Semantics

## Successful replace oracle

When `existing != NULL`, the harness expects replacement to succeed.

The oracle checks:

- `replace_ok` must be true
- object size must remain unchanged
- the key must still be found afterward using the same lookup semantics

This corresponds to cJSON's intended behavior for replacement:

- the old item is removed
- the new item takes its place
- the overall object cardinality is preserved

## Missing-key failure oracle

When `existing == NULL`, the harness treats the operation as a miss-path.

The oracle checks:

- `replace_ok` must be false
- object size must remain unchanged

This is intentionally narrower than a full semantic no-op oracle. It validates
the observable replace contract without depending on deep tree comparison.

## Final structural oracle

The final oracle is:

1. print
2. parse

Specifically:

- `cJSON_PrintUnformatted(root)`
- `cJSON_Parse(printed)`

This validates that the resulting root tree remains structurally serializable
and parseable after a sequence of replacements.

## Replace API Semantics Reflected by the Harness

The harness is designed around how cJSON replacement actually works.

At a high level, `cJSON_ReplaceItemInObject*`:

1. duplicates the replacement key name into the replacement item
2. looks up the existing target item using either case-sensitive or
   non-case-sensitive matching
3. replaces the found item in place via `cJSON_ReplaceItemViaPointer`
4. deletes the displaced old item

Important consequences for the harness:

- on success, ownership of `replacement` transfers into `root`
- on failure, the harness must delete `replacement` itself
- replacement should not change object size
- replacement should leave the key reachable under the lookup semantics used by
  the selected API

## Ownership and Cleanup

## Root ownership

`root` owns all currently attached members and their subtrees.

At the end of the harness, `cJSON_Delete(root)` frees the current object graph.

## Replacement ownership before API call

Before replacement, the newly created `replacement` subtree is owned by the
harness.

## Replacement ownership after API call

If `replace_ok` is true:

- ownership of `replacement` transfers into `root`
- the harness must not delete `replacement` directly

If `replace_ok` is false:

- cJSON did not adopt the replacement subtree
- the harness must delete it explicitly

The code does exactly that:

- on failure -> `cJSON_Delete(replacement)`
- on success -> no direct delete of `replacement`

## Key ownership

`lookup_key` is always heap-allocated by `read_key` and must be freed once the
operation finishes.

## Input Encoding Summary

## Global structure

Input consumption order:

1. one byte for operation count
2. per operation:
   - one fuzz-derived key
   - one byte for case mode
   - bytes needed to build the replacement subtree
   - one byte for API selection

## Operation count

`raw_ops % 32` bounds the number of replace attempts, keeping execution cheap
while still allowing multiple cumulative mutations.

## Key generation

The harness always uses `read_key`; unlike the delete harness, it does not
explicitly bias toward known keys. Existing-key hits still happen when the fuzz
input happens to reproduce a seeded or previously replaced key.

## Replacement tree generation

The replacement subtree can be:

- scalar
- array
- object

with one recursion level of nested containers.

This gives more semantic coverage than a pure scalar replacement harness while
keeping complexity bounded.

## API selection

The low bit of `which_api` selects:

- `0` -> `cJSON_ReplaceItemInObject`
- `1` -> `cJSON_ReplaceItemInObjectCaseSensitive`

## Caveats and Assumptions

## Duplicate-key caveat

The success oracle assumes that post-replace lookup for `lookup_key` still
finds the relevant member.

That is safe for the current harness as written because:

- the initial seeded object uses unique keys
- replacement preserves the matched key name
- the harness does not add new object members when replace misses

If the harness were later extended to permit duplicate object keys, especially
case-colliding duplicates, post-lookup assertions would need careful review.

## Scope of semantic checking

This harness does not validate:

- the precise internal linked-list position of the replacement node
- exact pointer identity after replacement
- allocator-level behavior beyond ownership correctness
- deep semantic equality between the installed subtree and the original
  replacement subtree
- full-tree semantic equivalence on missing-key replace attempts
- in-memory semantic equivalence against the reparsed tree after printing

It instead validates observable API-level semantics.

## Enforced Invariants

The harness enforces these core invariants:

- replacing an existing key with the selected API must succeed
- successful replacement must preserve object size
- successful replacement must leave the key discoverable under the same lookup
  semantics
- replacing a missing key must fail without changing object size
- the final root tree must remain printable and parseable

## Patches

## Non-finite number oracle fix

An oracle false positive was discovered after the initial semantic checks were
added.

Cause:

- `make_replacement_item()` can generate arbitrary `double` values
- some of those values are non-finite, such as `NaN` or `Inf`
- cJSON does not round-trip such values through normal JSON text in a way that
  preserves the original in-memory number semantics

This created problems for the earlier oracle design:

- `snapshot_json()` originally used print/parse, which could transform a
  replacement subtree before it was even compared against the installed value
- the final `cJSON_Compare(root, parsed, 1)` check could fail even when cJSON
  behaved as expected for printing non-finite numbers

Patch:

- `snapshot_json()` was changed to use `cJSON_Duplicate(..., 1)` instead of
  print/parse
- the final round-trip semantic comparison was skipped when the root tree
  contained any non-finite number

Reasoning:

- duplication preserves the actual in-memory cJSON structure
- skipping semantic round-trip comparison for non-finite numbers avoids
  asserting a property that JSON text does not reliably preserve

This was an intermediate patch that reduced one false-positive path.

## `cJSON_Compare()` and `NaN` fix

A second false-positive path was later found even after snapshot creation had
been switched to `cJSON_Duplicate(..., 1)`.

Cause:

- `cJSON_Compare()` compares numbers via the internal `compare_double()`
  helper
- that helper uses epsilon-based arithmetic
- `NaN` does not compare equal to `NaN` under that logic

Effect on the harness:

- the installed-value oracle could fail when a replacement subtree contained a
  non-finite number, even if the replacement had been installed correctly
- the missing-key no-op oracle could fail when the root already contained a
  non-finite number from an earlier successful replace, because comparing the
  unchanged pre/post trees through `cJSON_Compare()` would still return false

Intermediate patch:

- semantic comparisons that relied on `cJSON_Compare()` were temporarily
  skipped when the compared subtree contained any non-finite number

Reasoning:

- this avoided asserting semantic equality through a comparison routine that did
  not model non-finite numeric equality in a stable way

This was also an intermediate patch rather than the final design.

## Removal of `cJSON_Compare()`-based replace oracles

The compare-based subtree semantic checks were later removed from the replace
harness.

Removed checks:

- installed-value comparison between the post-replace item and a replacement
  snapshot
- full-tree no-op semantic comparison for missing-key replace attempts
- final semantic equality check between the in-memory root and the reparsed JSON
  tree

Reason:

- these checks were increasingly validating `cJSON_Compare()` behavior and JSON
  round-trip properties rather than the core replace API semantics
- fuzz-generated replacement values can include non-finite numbers, which made
  compare-based semantic equality brittle and patch-heavy
- the remaining invariants are more directly tied to replace correctness and are
  less likely to cause false positives

Current oracle set after this change:

- successful replace must return success
- successful replace must preserve object size
- successful replace must leave the key reachable via the selected lookup
  semantics
- missing-key replace must fail
- missing-key replace must preserve object size
- final printed output must still be parseable

This patch intentionally favors stable API-level invariants over deeper semantic
comparison for fuzz-generated replacement subtrees.
