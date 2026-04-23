# MKS C Unit Tests

These tests exercise C-level runtime components directly, without comparing full
program stdout against golden files.

## Build

From the repository root:

```sh
cmake --build build
```

This builds both:

- `build/mks` - the CLI interpreter
- `build/mks_unit_tests` - the C unit test binary

If `build/` does not exist yet:

```sh
cmake -S . -B build
cmake --build build
```

## Run

Run only the unit tests:

```sh
./build/mks_unit_tests
```

Run through CTest:

```sh
ctest --test-dir build --output-on-failure
```

Run the older black-box integration tests:

```sh
./tests.sh
```

## How They Work

`tests/unit/test_runtime.c` is a small self-contained test runner. It uses simple
`ASSERT_*` macros and returns a non-zero exit code if any assertion fails.

Current coverage includes:

- lexer integer vs float tokenization
- preservation of large `int64_t` literals above `2^53`
- float arithmetic result typing
- GC root scope stack restoration
- `NativeFn(MKSContext *ctx, ...)` context passing
- `mks_run_source(...)` returning an error status instead of exiting on syntax errors
- creating a fresh context after an error and successfully running another source

Some tests intentionally trigger syntax errors to verify the runner error
boundary. Direct `./build/mks_unit_tests` runs will print those diagnostics.
CTest hides successful test output unless `--output-on-failure` needs it.
