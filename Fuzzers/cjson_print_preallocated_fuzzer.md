# `cjson_print_preallocated_fuzzer.c`

## Purpose

This harness exercises `cJSON_PrintPreallocated` with explicit buffer and
serialization oracles.

The original harness mostly:

- built a small object
- called `cJSON_PrintPreallocated`
- parsed whatever was left in the buffer

That reached the printing code, but it did not prove much about correctness.

This revised harness checks the actual contract of `cJSON_PrintPreallocated`:

- valid prints with a large enough buffer must succeed
- successful output must exactly match `cJSON_Print` or
  `cJSON_PrintUnformatted`
- too-small buffers must fail
- zero-length buffers must fail
- `NULL` buffer, `NULL` item, and negative length must fail
- printing must not write outside the advertised buffer length
- printing must not mutate the source tree
- safe printed output must parse back to the same semantic tree

It also keeps an arbitrary parsed-input path so the fuzzer still explores many
real parser-generated trees.

## High-Level Structure

The harness has two layers.

### 1. Structured print scenarios

The first byte controls how many deterministic scenario executions run.

Those scenarios exercise:

- a seeded nested object/array tree
- fuzz-derived scalar values
- fuzz-derived numeric values, including non-finite doubles
- raw-value items

For each item, the harness validates both:

- formatted printing
- unformatted printing

### 2. Supplemental parsed-tree path

After the structured loop, the remaining payload is parsed as a single JSON
text.

If parsing succeeds, the harness applies the same print-preallocated oracle set
to that parsed tree.

This preserves coverage over arbitrary trees while keeping strong correctness
checks.

## Core Serialization Oracle

The core helper is `verify_exact_output_oracles`.

For a given `cJSON *item`, it:

1. checks invalid-argument behavior
2. computes the dynamic expected strings using:
   - `cJSON_PrintUnformatted(item)`
   - `cJSON_Print(item)`
3. checks documented-safe-length success for both formats
4. checks larger-buffer success for both formats
5. checks too-small-buffer failure for both formats
6. checks zero-length-buffer failure for both formats

This keeps the main oracle centered on the actual API contract rather than on
secondary effects.

## Success Oracles

## `verify_success_case`

For a chosen format flag, the harness:

1. computes the expected output with the dynamic API
2. allocates a buffer whose advertised length is large enough
3. fills the full allocation with a sentinel byte
4. calls `cJSON_PrintPreallocated`
5. requires success
6. requires exact string equality with the expected dynamic output
7. requires a terminating NUL at the expected output boundary
8. requires a redzone beyond the advertised buffer length to remain unchanged
9. reprints the same tree again and requires identical dynamic output
10. reparses the preallocated output when that tree is safe to round-trip

This proves:

- the API succeeds when given enough space
- it produces the same bytes as the canonical dynamic printer
- it respects the caller’s buffer boundary
- it does not mutate the source tree during printing

## Buffer Sizes Used

Each item is tested with:

- documented-safe length: `strlen(expected) + 5`
- slack buffer: `strlen(expected) + 17`

This follows cJSON’s own API note: callers should allocate five bytes more than
the final printed length to be safe.

The harness therefore only requires success at sizes the library explicitly
documents as sufficient.

## Failure Oracles

## Too-small buffer

`verify_too_small_failure` advertises a buffer length of exactly
`strlen(expected)`.

That guarantees the buffer is missing space for the terminating NUL and must
therefore fail.

The harness requires:

- `cJSON_PrintPreallocated(...) == false`
- bytes in the redzone beyond the advertised length stay unchanged
- the source tree still prints identically afterward

It intentionally does **not** require any specific partial contents inside the
advertised buffer because the API is allowed to fail after writing some prefix.

That keeps the failure oracle aligned with the actual contract.

## Zero-length buffer

`verify_zero_length_failure` advertises length `0`.

The harness requires:

- failure
- the entire backing storage remains unchanged

This is a stronger no-write case because there is no valid writable range at
all.

## Invalid arguments

`verify_invalid_argument_behavior` checks:

- `cJSON_PrintPreallocated(NULL, buffer, n, fmt) == false`
- `cJSON_PrintPreallocated(item, NULL, n, fmt) == false`
- `cJSON_PrintPreallocated(item, buffer, -1, fmt) == false`

This follows the library implementation directly.

## Round-Trip Oracle

## `verify_roundtrip_from_text`

When a tree is safe for text round-tripping, the harness:

1. parses the printed output
2. requires successful parse
3. requires semantic equality with the original tree in both:
   - strict compare
   - loose compare

This is only enforced for trees that satisfy `tree_is_roundtrip_safe`.

## Why some trees are excluded

The round-trip compare oracle is skipped for trees containing:

- raw nodes
- non-finite numbers
- ambiguous duplicate object keys

Those categories are valid print targets, but they are not stable enough for a
parse-and-compare oracle:

- raw nodes are printed as literal fragments rather than structured trees
- non-finite numbers print as `null`
- ambiguous duplicate keys can make compare semantics unstable

The harness still fully checks exact preallocated output equality for those
cases.

## Structured Scenario Set

The structured loop dispatches `opcode % 4`.

### Scenario 0: Seeded tree

`run_seed_tree_scenario` builds a deterministic nested object containing:

- numbers
- strings
- booleans
- null
- nested object
- nested array

This scenario is the main printable-tree case.

It is fully round-trip safe, so it exercises:

- formatted success
- unformatted success
- documented-safe fit
- slack fit
- undersized failure
- zero-length failure
- parse-back semantic equality

### Scenario 1: Scalar value

`run_scalar_scenario` builds one fuzz-driven scalar:

- number
- string
- true
- false
- null

This gives direct scalar coverage for the print path without requiring a full
container tree.

### Scenario 2: Number value

`run_number_scenario` builds a number directly from fuzz bytes interpreted as a
double.

This intentionally includes:

- ordinary finite numbers
- `NaN`
- infinities

For non-finite values, cJSON printing semantics normalize to `null`, so this
scenario is useful for exact-output checking even though parse-back equality is
not enforced there.

### Scenario 3: Raw value

`run_raw_scenario` builds a `cJSON_Raw` item from a small fixed set:

- `null`
- `true`
- `false`
- `0`
- `"raw"`
- `[]`
- `{}`

This targets the raw-print branch directly.

As with other raw cases, the round-trip compare oracle is skipped, but exact
preallocated-vs-dynamic output equality is still enforced.

## Supplemental Parsed-Tree Path

## `run_parsed_tree_scenario`

After the structured loop, the remaining payload is copied to a NUL-terminated
buffer and parsed as JSON.

If parsing succeeds, the harness applies the full print-preallocated oracle set
to that parsed tree.

This means arbitrary parser-produced trees also receive:

- invalid-argument checks
- exact output checks
- slack-buffer checks
- undersized failure checks
- zero-length failure checks
- no-redzone-overwrite checks
- no-mutation checks
- round-trip equality when safe

## Ownership and Safety Model

The harness is careful about buffer ownership because this API writes directly
into caller memory.

### Buffer ownership

Every print check allocates its own backing storage and fills it with sentinel
bytes before the call.

That makes it possible to check:

- output bytes
- terminating NUL
- redzone preservation

### Redzone discipline

For success and too-small-failure cases, the harness allocates:

- advertised buffer length
- plus a 32-byte redzone

The API is only allowed to write inside the advertised range.

The harness therefore requires every redzone byte to remain unchanged.

This is a meaningful oracle because `cJSON_PrintPreallocated` must never behave
like a dynamic growable buffer.

### Tree ownership

Every scenario owns the `cJSON *item` it creates and deletes it after all
checks complete.

The parsed-tree path similarly owns and deletes the parsed root.

## Enforced Invariants Summary

The harness enforces these statements:

- valid documented-safe preallocated prints must succeed
- valid slack-buffer prints must succeed
- successful preallocated output must exactly match dynamic print output
- successful preallocated output must be NUL-terminated
- too-small buffers must fail
- zero-length buffers must fail
- `NULL` item, `NULL` buffer, and negative lengths must fail
- printing must not write past the advertised buffer length
- printing must not mutate the source tree
- safe printed output must parse back to a semantically equal tree

## Improvement Over The Original Harness

Compared to the original `print_preallocated` harness, this version adds:

- deterministic structured scenarios
- documented success-vs-failure buffer oracles
- explicit formatted and unformatted coverage
- exact byte-for-byte output comparison against canonical printers
- redzone overwrite detection
- invalid-argument coverage
- zero-length failure coverage
- no-mutation checks
- safe parse-back semantic equality checks
- a README documenting the oracle model and limits

That makes it a substantially stronger OSS-Fuzz target for serialization
correctness rather than just serialization reachability.
