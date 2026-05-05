# `cjson_add_to_object_fuzzer.c`

## Purpose

This harness exercises cJSON object-addition APIs with explicit API-level
semantic checks rather than using the calls only as mutation primitives.

It focuses on:

- successful add-to-object behavior
- invalid-input failure behavior
- detached-item reinsertion behavior
- reference-add behavior
- bounded nested object creation
- final print/parse structural validity

The harness is intentionally closer in style to the replace and delete
harnesses:

- it starts from a deterministic seeded tree
- it uses bounded fuzz-derived operations
- it checks postconditions after each operation
- it periodically rebuilds state so successful paths remain reachable

It does **not** attempt deep semantic round-trip equality after printing,
because fuzz-generated numbers and raw values can make that oracle brittle in
ways similar to the replace harness.

## Execution Flow

## 1. Entry point

`LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)` performs the full
harness lifecycle.

The function:

1. rejects `NULL` or empty input
2. creates a root object
3. seeds the root with a fixed structure
4. creates shared reference source values
5. consumes one byte for operation count
6. executes up to `raw_ops % 32` add-oriented operations
7. runs an auxiliary add-reference-to-array validity check each iteration
8. optionally rebuilds the seeded state
9. prints and reparses the final tree
10. frees all owned objects

## 2. Seeded JSON shape

`seed_object(root, containers, &container_count)` creates a deterministic
starting object:

- `alpha` -> number
- `Bravo` -> string
- `CHARLIE` -> bool
- `delta` -> null
- `nestedObj` -> object with one child
- `childObj` -> object with one child
- `items` -> array with two elements

The harness also registers a bounded set of known object containers in the
`containers[]` table. Only object nodes go into this table.

This seeded shape is useful for two reasons:

- the fuzzer can revisit meaningful existing object targets instead of only the
  root
- rebuilds prevent the state space from degenerating into only newly created
  containers

## 3. Per-operation flow

Each loop iteration:

1. reads an opcode
2. selects a target object from `containers[]`
3. reads a `fail_mode` byte
4. chooses either a known key or a fuzz-derived key
5. optionally nulls the key and/or target to exercise failure paths
6. dispatches one add-oriented operation
7. runs a separate add-reference-to-array helper with either valid or invalid
   parent input
8. optionally rebuilds the root

The `valid_inputs` predicate is intentionally simple:

- valid -> `target != NULL && key != NULL`
- invalid -> either `target == NULL` or `key == NULL`

This keeps the semantic oracle aligned with the main observable contract of the
object-add APIs under test.

## Function-By-Function Documentation

## Input helpers

### `read_u8`

Reads one byte if available.

Behavior:

- returns `1` on success
- returns `0` when input is exhausted
- advances `*data`
- decrements `*size`

### `read_bytes`

Reads exactly `n` bytes into an output buffer.

Behavior:

- returns `1` on success
- returns `0` if too few bytes remain

### `read_bounded_string`

Creates a fuzz-derived heap string.

Behavior:

- consumes one byte for raw length
- bounds the length to `max_len`
- truncates further if remaining input is shorter
- allocates a NUL-terminated string
- replaces embedded NUL bytes with `'A'`

### `read_double`

Constructs a `double` from up to eight fuzz bytes.

Behavior:

- copies up to eight bytes into a `uint64_t` backing store
- interprets that backing store as a `double`
- therefore allows finite and non-finite values

## Key helpers

### `dup_cstr`

Allocates a heap copy of a C string.

Used by the known-key path.

### `choose_known_key`

Returns a heap copy of one seeded key.

This intentionally increases the probability of:

- adding to existing seeded containers
- exercising duplicate-key insertion behavior
- hitting stable, printable keys during debugging

### `choose_add_key`

Selects how an add key is generated.

Behavior:

- reads a mode byte
- on one path chooses a known seeded key
- otherwise returns a fuzz-derived string

This balances:

- targeted key reuse
- arbitrary key exploration

## Tree construction and rebuild helpers

### `seed_object`

Builds the deterministic initial object graph and refreshes the known container
registry.

Only object nodes are inserted into `containers[]`.

### `clear_root_object`

Removes every current child from `root` by repeatedly detaching `root->child`
with `cJSON_DetachItemViaPointer` and then deleting the detached subtree.

This preserves ownership correctly.

### `rebuild_object`

Replaces the current root contents with the original seeded shape.

### `maybe_rebuild`

Consumes one control byte and rebuilds when `(control & 7) == 0`.

This mirrors the delete harness strategy: successful paths remain reachable even
after many cumulative additions.

## Item-construction helpers

### `create_leaf_item`

Creates a scalar cJSON node from fuzz input.

Possible outputs include:

- number
- string
- raw
- bool
- true
- null

### `create_add_item`

Creates a bounded cJSON subtree used by the direct-item insertion path.

Behavior:

- reads one kind byte
- emits a leaf item for most cases
- can also build a shallow object or array
- limits recursion depth and fan-out

This gives the harness more semantic coverage than only wrapper helpers like
`cJSON_AddNumberToObject`, while keeping the tree small and stable.

## Oracle helper

### `assert_add_postconditions`

This is the core semantic checker.

It receives:

- `target`
- `key`
- whether inputs were valid
- whether the API reported success
- object size before the call
- object size after the call

It enforces two cases.

#### Invalid-input oracle

When `target == NULL` or `key == NULL`:

- the add operation must fail
- target size must remain unchanged

This models the expected no-op behavior for clearly invalid object-add calls.

#### Successful-add oracle

When `target != NULL` and `key != NULL`:

- the add operation must report success
- object size must increase by exactly one
- `cJSON_GetObjectItemCaseSensitive(target, key)` must find a member afterward

This deliberately avoids over-asserting deeper semantics about duplicate keys
or exact linked-list position.

## Operation helpers

### `op_add_number`

Exercises `cJSON_AddNumberToObject`.

Oracle:

- valid inputs -> success, size `+1`, key reachable
- invalid inputs -> failure, size unchanged

### `op_add_string`

Exercises `cJSON_AddStringToObject` with fuzz-derived payload.

Oracle is identical to the number case.

### `op_add_raw`

Exercises `cJSON_AddRawToObject`.

This covers raw-value insertion without asserting that the raw payload is valid
JSON text.

### `op_add_true`, `op_add_false`, `op_add_null`, `op_add_bool`

Exercise the scalar wrapper helpers:

- `cJSON_AddTrueToObject`
- `cJSON_AddFalseToObject`
- `cJSON_AddNullToObject`
- `cJSON_AddBoolToObject`

All use the same size-and-reachability oracle.

### `op_add_object`

Exercises `cJSON_AddObjectToObject`.

Additional behavior:

- on success, the returned object is registered in `containers[]` if space
  remains

This allows future iterations to add into newly created nested objects.

### `op_add_array`

Exercises `cJSON_AddArrayToObject`.

Additional behavior:

- on success, the new array is populated with two elements

The array itself is the object member being checked by the oracle.

### `op_add_reference`

Exercises `cJSON_AddItemReferenceToObject` using a shared string node.

Oracle:

- valid inputs -> success, size `+1`, key reachable
- invalid inputs -> failure, size unchanged

This checks reference insertion without transferring ownership of the original
shared node.

### `op_add_item_raw`

Exercises direct-item insertion through `cJSON_AddItemToObject` using a
fuzz-generated subtree.

Ownership behavior:

- on success, ownership transfers to `target`
- on failure, the harness deletes the unattached item

### `op_add_item_cs`

Exercises `cJSON_AddItemToObjectCS`.

The harness uses a simple number node and the same postconditions as the other
object-add APIs.

### `op_add_detached`

Exercises a detach-then-add path.

Flow:

1. create `tmp`
2. attach it to `root` under `oldkey`
3. detach `oldkey`
4. verify that `oldkey` is gone from `root`
5. add the detached node into the selected target under the requested key

Oracle:

- detachment must actually remove `oldkey` from `root`
- the subsequent add must satisfy the normal add oracle

This separately validates ownership transfer across detach and add.

### `op_invalid_reference_array_call`

This helper preserves some invalid-input API coverage for
`cJSON_AddItemReferenceToArray` while making it observable.

Behavior:

- either calls the API with `parent == NULL`
- or creates a temporary array and calls it with valid parent input

Oracle:

- `NULL` parent -> must fail
- valid parent -> must succeed and increase array size by one

This is not the main focus of the harness, but it replaces the previous
unchecked spray call with an explicit invariant.

## Operation Dispatch

The main loop dispatches `opcode % 12` across the add-focused helper set:

- number add
- string add
- raw add
- true add
- false add
- null add
- bool add
- object add
- array add
- reference add
- direct item add
- `AddItemToObjectCS` or detach-then-add depending on an opcode bit

## Oracle Semantics

## Core object-add oracle

The main semantic statement enforced by the harness is:

- valid object + valid key -> add succeeds, size increases by exactly one,
  key becomes reachable
- invalid object or invalid key -> add fails without changing the target size

This is intentionally API-level rather than deep semantic equality.

It does **not** assert:

- exact linked-list position of the new child
- exact pointer returned by future lookup when duplicate keys exist
- semantic equality after a print/parse round-trip

Those stronger properties would be more fragile and less directly tied to the
observable contract of these APIs.

## Duplicate-key behavior

The harness permits duplicate keys because cJSON object-add APIs can append new
members even when a matching key already exists.

The oracle therefore checks:

- size increase
- post-add reachability of the key

rather than requiring lookup to return the exact newly added node.

## Final structural oracle

At the end of the loop the harness performs:

1. `cJSON_PrintUnformatted(root)`
2. `cJSON_Parse(printed)`

This preserves the basic invariant that the final tree remains printable and the
printed text remains parseable.

Unlike the delete harness, there is no final `cJSON_Compare(root, parsed, 1)`
check. That is deliberate because fuzz-generated numbers and raw values can make
round-trip semantic equality unnecessarily brittle.

## Ownership and Cleanup

## Root ownership

`root` owns every successfully attached child subtree.

At function exit, `cJSON_Delete(root)` frees the full attached graph.

## Shared reference ownership

`shared_value` and `shared_array_item` are standalone source nodes used for
reference insertion.

When they are added by reference:

- the target receives a reference node
- the original shared node remains owned by the harness

The harness deletes both shared sources at the end.

## Direct-item ownership

For `cJSON_AddItemToObject*` paths:

- success -> ownership transfers into the target object
- failure -> the harness must delete the unattached item

The code follows that contract explicitly.

## Detached ownership

After `cJSON_DetachItemFromObject(root, "oldkey")` succeeds:

- `root` no longer owns the detached node
- the harness owns it temporarily
- ownership transfers again if the subsequent add succeeds
- otherwise the harness deletes it directly

## Key ownership

Any heap-allocated key returned by `choose_add_key` is freed at the end of the
iteration.

## Input Encoding Summary

## Global structure

Input bytes are consumed in this order:

1. one byte for operation count
2. per iteration:
   - one byte for opcode
   - one byte for target selector
   - one byte for `fail_mode`
   - bytes for key selection/generation
   - bytes needed by the selected operation
   - one byte for the auxiliary array-reference helper
   - one byte for optional rebuild control

## `fail_mode`

`fail_mode` is used to create invalid-input paths.

Current behavior:

- low two bits can null out `key`
- next two bits can null out `target`

This gives the fuzzer stable access to both:

- successful add paths
- explicit failure/no-op paths

## Caveats and Assumptions

## Scope of correctness

This harness checks focused API-level invariants. It does not try to prove every
possible semantic detail of object insertion.

In particular, it does not validate:

- duplicate-key ordering semantics
- exact pointer identity of the newly appended item under future lookup
- round-trip semantic equality of raw values and non-finite numbers
- internal linked-list structure

## Raw values

`cJSON_AddRawToObject` and `cJSON_CreateRaw` accept payloads that may not be
valid JSON literals.

That is acceptable here because the purpose is to fuzz API behavior and object
insertion mechanics. The final print/parse step is best-effort structural
validation rather than a requirement that every individual raw payload be
parsable in isolation.

## Non-finite numbers

Because the harness can synthesize arbitrary `double` values, including `NaN`
and infinities, it avoids final semantic comparison against the reparsed tree.

This matches the stability lessons from the replace harness.

## Enforced Invariants

The harness enforces these core invariants:

- valid add-to-object calls must succeed
- successful adds must increase object size by exactly one
- successful adds must leave the key reachable with case-sensitive lookup
- invalid target/key inputs must fail without changing object size
- detach-then-add must actually remove the detached source key before reinsertion
- valid add-reference-to-array calls must grow the array by one
- invalid add-reference-to-array calls with `NULL` parent must fail
- the final root tree must remain printable and reparsable

## Patches / Improvements Over The Original Harness

The original harness mainly invoked add APIs without checking whether the calls
produced correct observable effects.

This revised harness adds:

- deterministic seeded object state
- bounded nested container tracking
- explicit valid-input vs invalid-input oracles
- per-operation size checks
- key reachability checks after successful adds
- checked detach-then-add behavior
- checked add-reference-to-array behavior
- periodic rebuild to keep successful paths reachable
- a README documenting semantics, ownership, and intended invariants

This makes the harness substantially more useful as a semantic fuzz target
instead of a pure API exerciser.
