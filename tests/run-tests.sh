#!/bin/sh
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/talking-scale-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
compiler=${CXX:-c++}

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/reading_test.cpp" -o "$build_dir/reading-test"
"$build_dir/reading-test"

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/button_test.cpp" -o "$build_dir/button-test"
"$build_dir/button-test"

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -D__AVR_ATmega328P__ -DF_CPU=16000000UL \
  -I "$test_dir/stubs" \
  "$test_dir/sketch_test.cpp" \
  -o "$build_dir/sketch-test"

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/calibration_test.cpp" -o "$build_dir/calibration-test"
"$build_dir/calibration-test"

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/filter_test.cpp" -o "$build_dir/filter-test"
"$build_dir/filter-test"

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/storage_test.cpp" -o "$build_dir/storage-test"
"$build_dir/storage-test"

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/announcement_test.cpp" -o "$build_dir/announcement-test"
"$build_dir/announcement-test"

"$compiler" -std=c++11 -Wall -Wextra -Werror -pedantic \
  "$test_dir/sensor_test.cpp" -o "$build_dir/sensor-test"
"$build_dir/sensor-test"

# Each invocation starts the actual sketch's globals from a fresh boot.
for scenario in speech button-menu calibration calibration-negative \
  cancel-zero cancel-prompt cancel-wait cancel-read calibration-failure \
  calibration-timeout erased-eeprom zero-factor nan-factor infinity-factor \
  startup-timeout sensor-recovery fault-menu-exit interrupt interrupt-calibration \
  phrase-bound calibration-noise late-cancel unstable-tare modes clipping clip-request; do
  "$build_dir/sketch-test" "$scenario"
done
