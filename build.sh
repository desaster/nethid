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

# Get version from git
# - On a tag: v1.0.0 -> 1.0.0
# - After a tag: v1.0.0-5-gabc1234 -> 1.0.0-5-gabc1234
# - No tags: abc1234
VERSION=$(git describe --tags --always --dirty 2>/dev/null | sed 's/^v//' || echo "unknown")
echo ":: Version: $VERSION"

if [ ! "$USE_DOCKER" = "1" ] || [ -f /.dockerenv ]; then
    # Build web assets if web/dist doesn't exist or SKIP_WEB is not set
    if [ "$SKIP_WEB" != "1" ] && [ -d "web" ]; then
        if [ -f "web/package.json" ]; then
            echo ":: Building web assets"
            # Generate version file for web
            echo "export const VERSION = \"$VERSION\";" > web/src/version.ts
            (cd web && npm run build)
            echo ":: Generating fsdata.c"
            python3 scripts/makefsdata.py web/dist src/httpd/fsdata.c
        fi
    fi

    # Always reconfigure cmake to pick up version changes
    echo ":: Running cmake"
    cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DVERSION_STRING="$VERSION"

    echo ":: Running make"
    make -C build
else
    echo ":: Running docker container"
    docker run -it --rm -v $(pwd):/work nethiddev ./build.sh
fi
