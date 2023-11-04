#!/bin/sh
#
# To build without docker:
#   WIFI_SSID=foo WIFI_PASSWORD=bar ./build.sh
#
# or...
#
# To build with docker, first build the docker image:
#   docker build -t nethiddev:latest .
#
# Then build the project itself:
#   WIFI_SSID=foo WIFI_PASSOWRD=bar USE_DOCKER=1 ./build.sh

set -e

USE_DOCKER="${USE_DOCKER:-0}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico-sdk}"
export PICO_BOARD=pico_w

if [ ! "$USE_DOCKER" = "1" ] || [ -f /.dockerenv ]; then
    if [ ! -d "./build" ]
    then
        echo ":: Running cmake"
        cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1
    fi

    echo ":: Running make"
    make -C build
else
    echo ":: Running docker container"
    docker run -it --rm -v $(pwd):/work -e WIFI_SSID="$WIFI_SSID" -e WIFI_PASSWORD="$WIFI_PASSWORD" nethiddev ./build.sh
fi
