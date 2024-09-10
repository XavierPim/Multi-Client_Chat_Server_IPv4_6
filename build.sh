#!/usr/bin/env bash

if [ ! -f "./CMakeLists.txt" ]; then
  echo "You must run ./generate-cmakelists.sh first"
  exit 1
fi

if [ ! -d "./build/" ]; then
  echo "You must run ./change-compiler.sh first"
  exit 1
fi

cmake --build build --clean-first
