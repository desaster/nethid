# NetHID

## Wi-Fi controlled virtual USB keyboard and mouse

This firmware turns the [Pico W](https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html)
microcontroller board into a virtual keyboard and mouse, controllable over
wireless network.

As a hardware solution, it appears as a normal USB input device and requires
no extra software on the target system.

## Possible use cases

* Remote media controls (volume, playback)
* Use your laptop or mobile phone as a wireless keyboard/mouse for your desktop
* Automation, e.g. scheduled sleep / wakeup from homeassistant

## Status

Basic functionality is working.

TODO:

* Media keys
* Other protocols (MQTT? HTTP?)
* User configurable WiFi credentials
* Android app
* Improve latency

## Building

To build without docker:
```
WIFI_SSID=foo WIFI_PASSWORD=bar ./build.sh
```

or...

To build with docker, first build the docker image:

```
docker build -t nethiddev:latest .
```

Then build the project itself:

```
WIFI_SSID=foo WIFI_PASSOWRD=bar USE_DOCKER=1 ./build.sh
```

After a successful build, the resulting binary should be available in
`build/nethid.uf2`.

## Flashing

(TODO)

## How to use

(TODO)
