#!/usr/bin/env bash

set -u

export ASAN_OPTIONS=detect_leaks=0

BIN="./build/mks"
TEST_DIRS=("./tests/cases" "./tests/modules")
EXPECTED_DIR="./tests/expected"

passed=0
failed=0

if [ ! -x "$BIN" ]; then
  echo "Binary not found: $BIN"
  echo "Build first: cmake --build build"
  exit 1
fi

run_test() {
  local input_file="$1"
  local test_name
  local expected_file
  local output_file

  test_name="$(basename "$input_file" .mks)"
  expected_file="$EXPECTED_DIR/${test_name}.txt"
  output_file="$(mktemp)"

  if [ "$test_name" = "std_io_01" ]; then
    printf 'word\nrest of line\n' | "$BIN" "$input_file" >"$output_file" 2>&1
  else
    "$BIN" "$input_file" >"$output_file" 2>&1
  fi
  local exit_code=$?

  if [ ! -f "$expected_file" ]; then
    echo "[SKIP] $test_name (missing expected file)"
    rm -f "$output_file"
    return
  fi

  if diff -u "$expected_file" "$output_file" >/dev/null; then
    echo "[PASS] $test_name"
    passed=$((passed + 1))
  else
    echo "[FAIL] $test_name"
    echo "---- expected ----"
    cat "$expected_file"
    echo "---- got ----"
    cat "$output_file"
    echo "------------------"
    failed=$((failed + 1))
  fi

  if [ $exit_code -ne 0 ]; then
    echo "  note: program exited with code $exit_code"
  fi

  rm -f "$output_file"
}

for test_dir in "${TEST_DIRS[@]}"; do
  if [ ! -d "$test_dir" ]; then
    continue
  fi
  if [ "$test_dir" = "./tests/modules" ]; then
    while IFS= read -r -d '' file; do
      run_test "$file"
    done < <(find "$test_dir" \
      \( -path "*/src/*" -o -path "*/mks_modules/*" \) -prune -o \
      -type f -name "*.mks" -print0 | sort -z)
  else
    while IFS= read -r -d '' file; do
      run_test "$file"
    done < <(find "$test_dir" -type f -name "*.mks" -print0 | sort -z)
  fi
done

echo
echo "Passed: $passed"
echo "Failed: $failed"

if [ $failed -ne 0 ]; then
  exit 1
fi

exit 0
