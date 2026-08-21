#!/bin/bash
#
# Copyright (c) 2026.
# |  IMPORTANT NOTE: This file contains  proprietary  information  which  is   |
# |  private to Airobotics Ltd. You are forbidden to allow  any of the         |
# |  information, code, algorithms, methods etc. contained herein to come to   |
# |  the notice of parties  not specifically authorized for  that purpose by   |
# | Airobotics Ltd.                                                            |
#



cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug -j4