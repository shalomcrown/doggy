#!/bin/bash




cmake -S . -B cmake-build-release
cmake --build cmake-build-release -j4 -DCMAKE_BUILD_TYPE=Release --target package
