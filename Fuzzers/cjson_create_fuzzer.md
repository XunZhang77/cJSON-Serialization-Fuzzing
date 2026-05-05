# `cjson_create_fuzzer.c`

## Purpose

This harness exercises the cJSON creation APIs with explicit semantic oracles.

The earlier `create` harness mostly focused on the four `Create*Array` helpers
and then only checked whether the resulting array could be printed and maybe
parsed.

This revised harness expands the scope to cover:

- primitive creators
- string and raw creators
- reference creators
- array helpers
- ownership and aliasing behavior
- empty-array behavior
- invalid-argument behavior
- round-trip safety where that oracle is semantically valid

It is designed to prove actual create-function correctness, not just reach the
code.

## Important Contract Note

In this cJSON version:

- `cJSON_CreateIntArray(NULL, 0)` returns `NULL`
- `cJSON_CreateFloatArray(NULL, 0)` returns `NULL`
- `cJSON_CreateDoubleArray(NULL, 0)` returns `NULL`
- `cJSON_CreateStringArray(NULL, 0)` returns `NULL`

The implementation requires a non-`NULL` input pointer even when `count == 0`.

That is different from what the previous harness assumed, so this harness uses
the implementation’s actual contract.

## High-Level Structure

The harness has one structured scenario loop.

The first byte controls how many scenarios run:

- `case_count = raw_cases % 24`

Each iteration consumes one opcode and dispatches one of eight scenario
families:

1. primitive creators
2. string and raw creators
3. string reference creator
4. object/array reference creators
5. int array creator
6. float array creator
7. double array creator
8. string array creator

The remaining bytes parameterize values, strings, counts, and mutations.

## Common Oracles

## 1. Fresh-item metadata

For top-level items returned directly from create functions, the harness checks:

- the base type is correct
- `next == NULL`
- `prev == NULL`

For empty objects and arrays it also checks:

- `child == NULL`
- `cJSON_GetArraySize(item) == 0`

This confirms the newly created node starts detached and well-formed.

## 2. Numeric correctness

For numbers created through:

- `cJSON_CreateNumber`
- `cJSON_CreateIntArray`
- `cJSON_CreateFloatArray`
- `cJSON_CreateDoubleArray`

the harness checks:

- `valuedouble` matches the input value
- `valueint` follows cJSON’s saturation rule
- `cJSON_GetNumberValue` is consistent

For `NaN`, the harness checks `isnan` rather than direct equality.

## 3. Round-trip safety

`verify_roundtrip_if_safe` prints a tree with `cJSON_PrintUnformatted`, reparses
it, and requires semantic equality with `cJSON_Compare`.

This oracle is applied only when the tree is safe for that comparison:

- no raw nodes
- no non-finite numbers
- no ambiguous duplicate object keys

That keeps the round-trip oracle aligned with actual cJSON semantics.

## 4. Ownership vs aliasing

The harness distinguishes copied data from referenced data.

For copied creators:

- `cJSON_CreateString`
- `cJSON_CreateRaw`
- `cJSON_CreateStringArray`
- numeric array creators

the harness mutates the original input buffers after creation and requires the
created cJSON values to remain unchanged.

For reference creators:

- `cJSON_CreateStringReference`
- `cJSON_CreateObjectReference`
- `cJSON_CreateArrayReference`

the harness mutates the source object or source buffer and requires the created
reference item to observe that change.

That is one of the most important semantic distinctions in this API family.

## Global Invalid-Argument Oracle

`verify_global_invalid_behavior` is run for every input.

It checks:

- `cJSON_CreateString(NULL) == NULL`
- `cJSON_CreateRaw(NULL) == NULL`
- all `Create*Array(NULL, positive_count)` calls return `NULL`
- all `Create*Array(NULL, 0)` calls return `NULL`
- all `Create*Array(valid_pointer, -1)` calls return `NULL`

This captures the key invalid-input rules implemented in cJSON.

## Structured Scenario Set

## Scenario 0: Primitive creators

`run_primitive_scenario` exercises:

- `cJSON_CreateNull`
- `cJSON_CreateTrue`
- `cJSON_CreateFalse`
- `cJSON_CreateBool`
- `cJSON_CreateNumber`
- `cJSON_CreateArray`
- `cJSON_CreateObject`

The harness checks:

- type correctness
- fresh-item linkage
- empty object/array state
- exact printed text for null/true/false/empty array/empty object
- round-trip equality for round-trip-safe values
- non-finite number printing to `"null"`

This scenario establishes the base contract for the simplest creators.

## Scenario 1: String and raw creators

`run_string_and_raw_scenario` exercises:

- `cJSON_CreateString`
- `cJSON_CreateRaw`

For the string case it checks:

- type is `cJSON_String`
- `valuestring` content matches input
- `valuestring` storage is distinct from the source buffer
- mutating the source buffer does not mutate the created string
- safe round-trip equality

For the raw case it checks:

- type is `cJSON_Raw`
- `valuestring` content matches the chosen raw literal
- `valuestring` storage is distinct from the source buffer
- mutating the source buffer does not mutate the created raw value
- the printed text exactly matches the chosen literal

The raw literals are chosen from a fixed valid set such as:

- `null`
- `true`
- `false`
- `0`
- `"raw"`
- `[]`
- `{}`

The harness does not require parse-back equality for raw nodes because raw
printing serializes the raw literal, not a cJSON node of the same type.

## Scenario 2: String reference creator

`run_string_reference_scenario` exercises `cJSON_CreateStringReference`.

It checks:

- base type is `cJSON_String`
- `cJSON_IsReference` is set
- `valuestring` points to the original mutable source buffer
- mutating the source buffer is reflected through the cJSON item
- string-reference printing remains round-trip safe

This directly tests that the API aliases the caller’s storage instead of copying
it.

## Scenario 3: Object and array reference creators

`run_container_reference_scenario` exercises:

- `cJSON_CreateObjectReference`
- `cJSON_CreateArrayReference`

It checks both non-`NULL` and `NULL` child behavior.

For the non-`NULL` case:

- type is `cJSON_Object` or `cJSON_Array`
- `cJSON_IsReference` is set
- `child` points to the original number node
- mutating the original number is visible through the reference item

For the `NULL` case:

- the reference item is still created
- the base type and `cJSON_IsReference` bit are correct
- `child == NULL`

This matches the implementation, which does not reject `NULL` child pointers.

## Scenario 4: Int array creator

`run_int_array_scenario` exercises `cJSON_CreateIntArray`.

It checks:

- count `0` with a valid pointer produces an empty array
- positive count produces an array of the expected size
- every element is a number node with the correct value
- mutating the source `int` array after creation does not mutate the cJSON array
- safe round-trip equality for the produced array

It also preserves global invalid checks for `NULL` and negative counts.

## Scenario 5: Float array creator

`run_float_array_scenario` exercises `cJSON_CreateFloatArray`.

It checks:

- size and element count
- numeric conversion from `float` to `double`
- independence from the source float buffer after creation
- safe round-trip equality when all numbers are finite

For non-finite values the array is still validated structurally, but the
round-trip equality oracle is skipped.

## Scenario 6: Double array creator

`run_double_array_scenario` exercises `cJSON_CreateDoubleArray`.

It checks:

- size and element count
- exact stored numeric value (with `NaN` handled specially)
- source-buffer independence after creation
- safe round-trip equality when all numbers are finite

This is the widest numeric coverage because doubles are created directly from
fuzz bytes.

## Scenario 7: String array creator

`run_string_array_scenario` exercises `cJSON_CreateStringArray`.

It checks:

- count `0` with a valid pointer produces an empty array
- positive count produces the expected array size
- every element is a `cJSON_String`
- every element string equals the source string
- every element owns its own string storage
- mutating the source string buffer after creation does not mutate the array
- safe round-trip equality

This is the strongest ownership oracle in the array-helper set.

## Why there is no parsed-input path

Unlike parse, compare, duplicate, or print-preallocated, the create API family
is not driven by parsing an existing serialized representation.

The semantics that matter here are:

- what type is created
- whether data is copied or aliased
- what happens for empty counts
- what happens for invalid counts or `NULL` inputs
- whether produced arrays contain the correct elements

Those properties are better exercised by structured constructor scenarios than
by reparsing arbitrary JSON text and then creating unrelated objects from it.

The harness therefore uses fuzz input to parameterize construction directly
rather than routing through a supplemental parse step.

## Ownership Model

The harness follows cJSON ownership rules carefully.

### Copied creators

For copied creators, cJSON owns its internal storage after creation:

- strings
- raw values
- numeric arrays
- string arrays

The harness frees only the original source buffers it allocated.

### Reference creators

For reference creators, the caller still owns the referenced source:

- string-reference source buffers are kept alive until after the cJSON item is
  deleted
- referenced child nodes are deleted separately from the reference wrapper

The harness uses this distinction directly as part of the oracle set.

## Enforced Invariants Summary

The harness enforces these correctness statements:

- primitive creators return the correct base type
- fresh top-level created items are detached
- number creation preserves `valuedouble` and saturated `valueint`
- null/true/false/bool print correctly
- empty object/array creators return empty containers
- `CreateString` and `CreateRaw` copy source storage
- `CreateStringReference` aliases source storage
- `CreateObjectReference` and `CreateArrayReference` alias the child pointer
- `Create*Array(valid_pointer, 0)` produces an empty array
- `Create*Array(NULL, 0)` returns `NULL` in this cJSON version
- `Create*Array(NULL, positive_count)` returns `NULL`
- `Create*Array(valid_pointer, negative_count)` returns `NULL`
- created arrays contain the correct number of elements
- created array elements preserve the source values/strings
- mutating source arrays after creation does not mutate copied cJSON arrays
- round-trip parse/compare holds whenever that oracle is semantically safe

## Improvement Over The Previous Create Harness

Compared to the earlier create harness, this version adds:

- coverage for the full create API family instead of only array helpers
- explicit copied-vs-referenced storage checks
- explicit invalid-input checks
- correct handling of the `NULL + zero-count` array-helper contract
- semantic verification of element contents
- source-independence checks for copied arrays and strings
- aliasing checks for reference creators
- documentation of the precise oracle model and limits

That makes it a substantially stronger OSS-Fuzz harness for creation
correctness rather than just constructor reachability.
