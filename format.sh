#!/bin/bash

find . -name "*.cpp" -o -name "*.h" | grep -v -e build/ -e node_modules/ -e vendor/ | xargs clang-format -i
