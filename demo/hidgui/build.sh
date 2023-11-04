#!/bin/sh
set -e

if [ ! -d "./build" ]
then
    echo ":: Running cmake"
    cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1
fi

echo ":: Running make"
make -C build
