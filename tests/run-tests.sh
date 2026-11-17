#!/bin/sh
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
onebutton_dir=${ONEBUTTON_SOURCE_DIR:-}
if [ -z "$onebutton_dir" ]; then
  for candidate in "$HOME/Documents/Arduino/libraries/OneButton/src" "$HOME/Arduino/libraries/OneButton/src"; do
    if [ -f "$candidate/OneButton.cpp" ]; then
      onebutton_dir=$candidate
      break
    fi
  done
fi
if [ ! -f "$onebutton_dir/OneButton.cpp" ]; then
  echo "Install OneButton, or set ONEBUTTON_SOURCE_DIR to its src directory." >&2
  exit 1
fi

build_dir=$(mktemp -d "${TMPDIR:-/tmp}/talking-scale-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
compiler=${CXX:-c++}

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/reading_test.cpp" -o "$build_dir/reading-test"
"$build_dir/reading-test"

# OneButton's current headers use C++14 attributes. Its debounce implementation
# also has a signed comparison; suppress that warning only for the library file.
"$compiler" -std=c++14 -Wall -Wextra -Werror -pedantic -Wno-sign-compare \
  -I "$test_dir/stubs" -I "$onebutton_dir" \
  -c "$onebutton_dir/OneButton.cpp" -o "$build_dir/OneButton.o"
"$compiler" -std=c++14 -Wall -Wextra -Werror -pedantic \
  -D__AVR_ATmega328P__ -DF_CPU=16000000UL \
  -I "$test_dir/stubs" -I "$onebutton_dir" \
  "$test_dir/sketch_test.cpp" "$build_dir/OneButton.o" \
  -o "$build_dir/sketch-test"

# Each invocation starts the actual sketch's globals from a fresh boot.
for scenario in speech button-menu calibration calibration-negative \
  cancel-zero cancel-prompt cancel-wait cancel-read calibration-failure \
  calibration-timeout erased-eeprom zero-factor nan-factor infinity-factor \
  startup-timeout sensor-recovery fault-menu-exit; do
  "$build_dir/sketch-test" "$scenario"
done
