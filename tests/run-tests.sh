#!/bin/bash
set -e
make clean
make
leaks --atExit -- ./test_driver
