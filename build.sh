#!/bin/bash


cmake --preset native-release
cmake --build --preset native-release --target package
