# hidgui

SDL2 application that captures keyboard and mouse input and forwards it to a NetHID device over UDP.

## Usage

```
hidgui-rs <HOST> [--password <PASSWORD>]
```

Press RCTRL+Q to exit.

## Building

```
cargo build --release
```

First build compiles SDL2 from source (requires cmake and a C/C++ compiler).

## Cross-compiling for Windows

```
rustup target add x86_64-pc-windows-gnu
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
cargo build --release --target x86_64-pc-windows-gnu
```

Ship `hidgui-rs.exe` together with `SDL2.dll` from the same output directory.
