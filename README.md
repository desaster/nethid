# NetHID

## Wi-Fi controlled virtual USB keyboard and mouse

This firmware turns the [Pico W](https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html)
microcontroller board into a virtual keyboard and mouse, controllable over
wireless network.

As a hardware solution, it appears as a normal USB input device and requires
no extra software on the target system.

## Features

- USB HID support with keyboard, mouse, and media keys (consumer control)
- Web interface with virtual keyboard, mouse and touch support
- HTTP API for automation
- UDP for low(ish) latency clients

## Possible use cases

* Remote media controls (volume, playback)
* Use your laptop or mobile phone as a wireless keyboard
* Automation, e.g. scheduled sleep / wakeup from homeassistant

## WiFi Setup

On first boot (or when no WiFi is configured), the device starts in AP mode:

1. Connect to the `NetHID-XXXXXX` WiFi network (password: `nethid123`)
2. Open `http://192.168.4.1` in your browser
3. Select your WiFi network and enter the password
4. Device reboots and connects to your network

To return to AP mode later, hold the BOOTSEL button for 5+ seconds until the LED blinks rapidly, then release.

## Usage

Once connected to your network, access the web interface at the device's IP address or hostname.

The web UI provides:
- Virtual keyboard (TKL layout for desktop, compact layout for mobile)
- Mouse control via trackpad area
- Media keys (play/pause, volume, etc.)

For programmatic control, see the [API specification](openapi.yaml).

## Building

```bash
./build.sh
```

To build with docker:

```bash
docker build -t nethiddev:latest .
USE_DOCKER=1 ./build.sh
```

After a successful build, the resulting binary is available at `build/nethid.uf2`.

## Flashing

Hold the BOOTSEL button while connecting the Pico W via USB. It will appear as a USB drive. Copy `build/nethid.uf2` to the drive.

## License

MIT
