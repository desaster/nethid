#!/bin/sh
#
# Build script for NetHID
#
# WiFi credentials are now stored in flash and configured via the web interface.
# The device will start in AP mode if no credentials are configured.
#
# To build with docker, first build the docker image:
#   docker build -t nethiddev:latest .
#
# Then build the project itself:
#   USE_DOCKER=1 ./build.sh

set -e

USE_DOCKER="${USE_DOCKER:-0}"
SKIP_WEB="${SKIP_WEB:-0}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico-sdk}"
export PICO_BOARD=pico_w

if [ ! "$USE_DOCKER" = "1" ] || [ -f /.dockerenv ]; then
    # Build web assets if web/dist doesn't exist or SKIP_WEB is not set
    if [ "$SKIP_WEB" != "1" ] && [ -d "web" ]; then
        if [ -f "web/package.json" ]; then
            echo ":: Building web assets"
            (cd web && npm run build)
            echo ":: Generating fsdata.c"
            python3 scripts/makefsdata.py web/dist src/httpd/fsdata.c
        fi
    fi

    if [ ! -d "./build" ]
    then
        echo ":: Running cmake"
        cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1
    fi

    echo ":: Running make"
    make -C build
else
    echo ":: Running docker container"
    docker run -it --rm -v $(pwd):/work nethiddev ./build.sh
fi
