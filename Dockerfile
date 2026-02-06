FROM alpine:3.23.3

# Install toolchain
RUN apk update && \
    apk upgrade && \
    apk add git \
            python3 \
            py3-pip \
            cmake \
            build-base \
            libusb-dev \
            mbedtls-dev \
            bsd-compat-headers \
            newlib-arm-none-eabi \
            gcc-arm-none-eabi \
            g++-arm-none-eabi \
            linux-headers \
            bash \
            curl \
            nodejs \
            npm

# pico sdk
ARG SDK_PATH=/opt/pico_sdk
RUN git clone --depth 1 --branch 2.2.0 https://github.com/raspberrypi/pico-sdk $SDK_PATH && \
    cd $SDK_PATH && \
    git submodule update --init

ENV PICO_SDK_PATH=$SDK_PATH

# picotool
RUN git clone --depth 1 --branch 2.2.0 https://github.com/raspberrypi/picotool.git /home/picotool && \
    cd /home/picotool && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make && \
    make install && \
    rm -rf /home/picotool

RUN adduser -D dev
USER dev

WORKDIR /work
