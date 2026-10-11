#!/bin/bash

rm -rf build
mkdir build
cd build

cmake ..
cmake --build . --config Release -j "$(getconf _NPROCESSORS_ONLN)"
