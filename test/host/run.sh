#!/bin/sh
# Builds and runs the host-side tests of the sequence engine (needs a C++17 compiler, no Arduino).
set -e
cd "$(dirname "$0")"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -o /tmp/airbox_sequence_tests test_sequence.cpp
/tmp/airbox_sequence_tests
