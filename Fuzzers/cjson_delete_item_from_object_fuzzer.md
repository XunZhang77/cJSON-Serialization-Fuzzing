# `cjson_delete_item_from_object_fuzzer.c`

## Purpose

This harness exercises cJSON deletion and detachment behavior across object and
array APIs. It is built around a seeded JSON tree and a bounded sequence of
mutating operations derived from fuzzer input bytes.

The harness is not only looking for memory safety failures. It also encodes
post-operation semantic expectations for:

- object deletion
- nested object deletion
- array deletion by index
- nested array deletion by index
- detach-then-delete behavior
- final print/parse round-trip validity

The harness intentionally keeps the tree small and deterministic so that
individual API effects are easier to reason about and validate.

## Execution Flow

## 1. Entry point

`LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)` is the only exported
fuzzing entry point.

The function:

1. rejects `NULL` input or zero-length input
2. allocates a fresh root object with `cJSON_CreateObject()`
3. seeds the root with a known structure
4. consumes one byte to derive the number of operations
5. executes up to `raw_ops % 48` operations while input remains
6. optionally rebuilds the seeded tree after each operation
7. runs the final structural and semantic round-trip oracle
8. deletes the root

## 2. Seeded JSON shape

`seed_object(root)` creates a deterministic starting structure:

- root keys:
  - `alpha` -> number
  - `Bravo` -> number
  - `CHARLIE` -> number
  - `delta` -> string
  - `Echo` -> bool
  - `foxtrot` -> null
  - `nestedObj` -> object
  - `ArrayKey` -> array
- `nestedObj` contains:
  - `innerKey` -> string
  - `InnerNum` -> number
  - `innerFlag` -> bool
  - `innerArray` -> array
- `ArrayKey` contains three elements
- `nestedObj.innerArray` contains two elements

This fixed shape lets the harness meaningfully test both successful and
unsuccessful delete operations under case-sensitive and non-case-sensitive
matching.

## 3. Operation dispatch

Each loop iteration consumes one byte as `op_kind` and dispatches by
`op_kind % 5`:

- `0` -> delete from root object
- `1` -> delete from nested object
- `2` -> delete from root array
- `3` -> delete from nested array
- `4` -> detach-then-delete from root object

After each operation, `maybe_rebuild()` may restore the original seeded tree.

## 4. Final round-trip oracle

At the end of the loop:

1. the root is printed with `cJSON_PrintUnformatted`
2. the resulting JSON text is reparsed with `cJSON_Parse`
3. if parsing succeeds, `cJSON_Compare(root, parsed, 1)` is used to confirm
   semantic equivalence
4. all temporary objects are freed

This keeps the original structural oracle while strengthening it with a
post-round-trip semantic comparison.

## Function-By-Function Documentation

## Input helpers

### `read_u8`

Reads one byte if available.

Behavior:

- returns `1` on success
- returns `0` if fewer than one byte remain
- advances `*data`
- decrements `*size`

This is the basic bounded input primitive used throughout the harness.

### `read_u8_as_int`

Reads one byte and widens it to `int`.

Behavior:

- returns `0` if `read_u8` fails
- otherwise stores the widened byte into `*out`

This is used for array index generation.

## Key helpers

### `dup_cstr`

Allocates and copies a NUL-terminated C string.

Behavior:

- returns heap memory on success
- returns `NULL` on allocation failure

The harness uses it when selecting one of the known seeded keys.

### `read_key`

Creates a fuzz-controlled key string.

Behavior:

- consumes one byte for a raw length
- bounds the effective key length to `raw_len % 64`
- truncates further if remaining input is shorter
- allocates a new heap string
- copies the raw bytes into the key
- replaces embedded NUL bytes with `'A'`

The function guarantees the resulting key is a valid NUL-terminated string.

### `mutate_case`

Applies deterministic case mutations to a key string.

Supported modes after `% 4`:

- `0` -> unchanged
- `1` -> uppercase lowercase letters
- `2` -> mixed alternating case transformation
- `3` -> lowercase uppercase letters

This helps exercise differences between:

- `cJSON_GetObjectItem`
- `cJSON_GetObjectItemCaseSensitive`
- `cJSON_DeleteItemFromObject`
- `cJSON_DeleteItemFromObjectCaseSensitive`
- `cJSON_DetachItemFromObject`
- `cJSON_DetachItemFromObjectCaseSensitive`

### `choose_known_root_key`

Returns a heap copy of one seeded root key.

This increases the probability of hitting existing keys, which is important for
semantic delete oracles.

### `choose_known_nested_key`

Returns a heap copy of one seeded nested-object key.

This plays the same role for `nestedObj`.

### `choose_lookup_key`

Selects how a lookup key is generated.

Behavior:

- consumes a mode byte
- if `(mode & 3) != 0`, returns an arbitrary fuzz-derived key via `read_key`
- otherwise consumes a selector byte and chooses a known seeded key

The `nested_scope` flag determines whether the known-key set comes from the
root object or `nestedObj`.

This balancing is important:

- arbitrary keys explore miss behavior
- known keys explore hit behavior

## Tree construction and rebuild helpers

### `seed_object`

Builds the deterministic initial object graph used by the harness.

Allocation behavior:

- creates nested objects and arrays
- inserts them into the root if creation succeeds
- tolerates partial allocation failure without crashing

### `clear_root_object`

Empties the root object by repeatedly detaching `root->child` via
`cJSON_DetachItemViaPointer` and then deleting the detached node.

This preserves correct ownership:

- detachment removes the child from the root
- `cJSON_Delete` frees the detached subtree exactly once

### `rebuild_object`

Replaces the current root contents with the original seeded structure by:

1. clearing the root
2. calling `seed_object`

### `maybe_rebuild`

Consumes one control byte and rebuilds when `(control & 7) == 0`.

This periodically resets the state space so the fuzzer repeatedly revisits
successful delete paths instead of only progressing toward an empty tree.

## Oracle helpers

### `lookup_object_item`

Dispatches lookup to the matching cJSON API based on a boolean:

- case-insensitive -> `cJSON_GetObjectItem`
- case-sensitive -> `cJSON_GetObjectItemCaseSensitive`

This is critical because the semantic oracle must use the same matching
semantics as the operation under test.

### `snapshot_json`

Creates a semantic snapshot of a subtree by:

1. printing it with `cJSON_PrintUnformatted`
2. reparsing the printed text with `cJSON_Parse`

The result is a standalone cJSON tree used only for comparison.

This helper is used only when the operation is expected to be a no-op.

### `assert_semantic_equivalence_if_snapshotted`

Compares two trees with `cJSON_Compare(before, after, 1)` and aborts if they
are not semantically equivalent.

It is intentionally tolerant of missing snapshots:

- if `before == NULL`, it does nothing
- if `after == NULL`, it does nothing

The helper therefore avoids turning allocation failure in snapshot creation into
an oracle failure.

## Operation helpers

### `delete_from_root_object`

This operation targets the root object.

Flow:

1. choose a key
2. read a case-mutation mode
3. read an API selector
4. mutate the key
5. determine whether the target exists using the matching lookup API
6. if the target does not exist, snapshot the entire root
7. call either:
   - `cJSON_DeleteItemFromObject`
   - `cJSON_DeleteItemFromObjectCaseSensitive`
8. check the postcondition

Postconditions:

- if the key existed under that API’s lookup semantics before deletion, it must
  no longer be found with the same lookup semantics
- if the key did not exist before deletion, the root must remain semantically
  equivalent when a snapshot was available

### `delete_from_nested_object`

This operation is identical in structure to root-object delete, except that the
target parent is `nestedObj`.

Precondition:

- `nestedObj` must currently exist and still be an object

Postconditions are the same as the root-object version, but scoped to the
`nestedObj` subtree.

### `delete_from_root_array`

This operation targets `ArrayKey`.

Flow:

1. locate `ArrayKey`
2. read one byte as an integer
3. record `size_before = cJSON_GetArraySize(arr)`
4. map the byte to `index = (index % 8) - 2`
5. compute whether that index is valid
6. call `cJSON_DeleteItemFromArray`
7. record `size_after`
8. verify the size delta

Postconditions:

- valid index -> size must decrease by exactly one
- invalid index -> size must remain unchanged

The index mapping intentionally generates:

- negative indices
- in-range indices
- out-of-range positive indices

### `delete_from_nested_array`

This operation mirrors `delete_from_root_array`, but targets
`nestedObj.innerArray`.

Preconditions:

- `nestedObj` must exist and still be an object
- `innerArray` must exist and still be an array

Postconditions are identical to the root-array variant.

### `detach_then_delete_from_root`

This operation validates the detach path separately from direct delete.

Flow:

1. choose a key
2. read case mode and API selector
3. mutate the key
4. determine whether the target exists using matching lookup semantics
5. snapshot the root if the target does not exist
6. call either:
   - `cJSON_DetachItemFromObject`
   - `cJSON_DetachItemFromObjectCaseSensitive`
7. verify the parent no longer contains the detached item when it previously
   existed
8. delete the detached subtree exactly once

Postconditions:

- if the key existed, detach must return a non-`NULL` node and the parent must
  no longer expose that key under the same lookup semantics
- if the key did not exist, the root must remain semantically equivalent when a
  snapshot was available

## Oracle Semantics

## Object deletion oracle

Used in:

- `delete_from_root_object`
- `delete_from_nested_object`

The oracle records whether the target key exists before deletion using the same
lookup semantics as the API being exercised.

Why this matters:

- `cJSON_DeleteItemFromObject` matches with case-insensitive lookup
- `cJSON_DeleteItemFromObjectCaseSensitive` matches with case-sensitive lookup

The post-check therefore does not assume stronger semantics than the library
actually provides.

## No-op semantic oracle

Used when a delete or detach operation is expected to miss.

The oracle:

1. snapshots the relevant subtree before the operation
2. performs the operation
3. compares the current subtree with the snapshot using `cJSON_Compare`

This is metamorphic because the expectation is: changing the lookup key while
keeping it non-matching should preserve the parent tree.

The comparison is skipped if snapshot creation fails, which avoids introducing
failures from temporary allocation pressure.

## Array size oracle

Used in:

- `delete_from_root_array`
- `delete_from_nested_array`

The oracle depends only on array size, not on element identity.

That choice is intentional:

- it is a strong enough invariant to validate delete semantics
- it does not overfit to internal linked-list layout
- it keeps the harness simple and stable

## Detach oracle

Used in `detach_then_delete_from_root`.

The detach-specific correctness condition is:

- an existing item must be removed from the parent
- the detached subtree must become separately owned by the caller

The harness models caller ownership by always doing exactly one
`cJSON_Delete(detached)` after a successful or failed detach call.

`cJSON_Delete(NULL)` is safe, so this remains correct even for miss paths.

## Final structural and semantic round-trip oracle

The final oracle is intentionally two-stage:

1. `cJSON_PrintUnformatted(root)`
2. `cJSON_Parse(printed)`
3. `cJSON_Compare(root, parsed, 1)`

The first two steps preserve the original structural sanity check:

- the final tree should remain printable
- the printed JSON should remain parseable

The final compare strengthens this:

- reparsing should produce a semantically equivalent tree

Using `cJSON_Compare(..., 1)` is safe here because the harness builds and
mutates trees through cJSON itself and does not rely on formatting artifacts.

## Ownership and Cleanup

## Root ownership

`root` owns every subtree inserted into it until those subtrees are detached or
deleted.

At function exit, `cJSON_Delete(root)` frees all currently attached children.

## Detached ownership

After `cJSON_DetachItemFromObject*` succeeds:

- the parent no longer owns the detached subtree
- the caller becomes responsible for freeing it

The harness satisfies that contract by always calling `cJSON_Delete(detached)`.

## Snapshot ownership

Snapshots created by `snapshot_json()` are independent parsed trees. They are
used only for comparison and must always be released with `cJSON_Delete`.

## Temporary key ownership

Any heap-allocated key returned by:

- `read_key`
- `choose_known_root_key`
- `choose_known_nested_key`
- `choose_lookup_key`

is freed by the operation helper that consumes it.

## Input Encoding Summary

## Global structure

Input bytes are consumed in this order:

1. one byte for operation count
2. per operation:
   - one byte for operation kind
   - additional bytes consumed by the selected helper
   - one byte for optional rebuild control if still available

## Object delete operations

Bytes typically control:

- key generation mode
- either a known-key selector or arbitrary key contents
- case mutation mode
- API choice

## Array delete operations

Bytes typically control:

- one raw byte widened to `int`
- mapped to a small signed index range `[-2, 5]`

## Caveats and Assumptions

## Duplicate-key caveat

The object-delete oracle assumes that removing a matched key means that the same
lookup no longer finds a match afterward.

That is safe for the current harness because:

- the seeded tree uses unique keys
- this harness never adds new object entries

If the harness were later extended to create duplicate object keys, especially
case-colliding ones, the oracle would need to be revisited.

## Snapshot best-effort behavior

Snapshot creation can fail under allocation pressure. In that case, the harness
does not enforce the no-op semantic oracle for that operation.

This is deliberate: allocation failure in the oracle machinery should not create
false positives.

## Scope of correctness

This harness does not attempt to prove every semantic detail of deletion.

For example:

- it does not check exact array element identity after deletion
- it does not validate internal linked-list pointers directly
- it does not model all possible cJSON object states

It instead checks a focused set of invariants that should hold for normal cJSON
API behavior.

## Enforced Invariants

The harness enforces the following core invariants:

- deleting an existing object member removes it from future lookup under the
  same API semantics
- deleting a non-existing object member is a semantic no-op when snapshotting is
  available
- deleting a valid array index decreases array size by one
- deleting an invalid array index leaves array size unchanged
- detaching an existing member removes it from the parent and transfers
  ownership to the caller
- the final tree remains printable, parseable, and semantically stable across a
  print/parse round-trip

## Patches

## Current status

No patch has been needed yet for the delete harness’s semantic oracles.

The main reason is that this harness only mutates a fixed seeded tree and does
not synthesize arbitrary numeric replacement values. Its numeric content is
therefore limited to the finite seeded numbers already present in the tree.

That means the current print/parse-based snapshotting and final
`cJSON_Compare(root, parsed, 1)` check do not presently hit the non-finite
number false-positive issue that affected the replace harness.

Future caveat:

- if this harness is later extended to insert or generate arbitrary numeric
  values, especially `NaN` or `Inf`, then the same class of round-trip oracle
  problem could appear here as well
- in that case, snapshots should likely move to `cJSON_Duplicate(..., 1)` and
  final semantic round-trip comparison should be reconsidered for non-finite
  values
