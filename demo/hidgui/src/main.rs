mod auth;
mod keymap;
mod udp;

use clap::Parser;
use sdl2::event::Event;
use sdl2::hint;
use sdl2::keyboard::{Mod, Scancode};
use sdl2::mouse::MouseButton;

#[derive(Parser)]
#[command(about = "Forward keyboard/mouse input to a NetHID device")]
struct Args {
    /// Target host (hostname or IP)
    host: String,

    /// Device password
    #[arg(long)]
    password: Option<String>,
}

fn main() {
    let args = Args::parse();

    let token = auth::authenticate(&args.host, args.password.as_deref());
    match &token {
        Some(_) => println!("Authenticated (v2 packets)"),
        None => println!("No auth required (v1 packets)"),
    }

    let sender = udp::UdpSender::new(&args.host, token);

    let sdl = sdl2::init().expect("failed to init SDL2");
    let video = sdl.video().expect("failed to init SDL2 video");

    hint::set("SDL_GRAB_KEYBOARD", "1");

    let window = video
        .window("NetHID - Press RCTRL+Q to quit", 640, 480)
        .position_centered()
        .build()
        .expect("failed to create window");

    let mut canvas = window
        .into_canvas()
        .build()
        .expect("failed to create canvas");

    canvas.window_mut().set_grab(true);
    sdl.mouse().show_cursor(false);
    sdl.mouse().warp_mouse_in_window(canvas.window(), 320, 240);

    let mut event_pump = sdl.event_pump().expect("failed to get event pump");
    let mut mouse_buttons: u8 = 0;

    println!("RCTRL+Q to quit!");

    canvas.set_draw_color(sdl2::pixels::Color::RGB(0x80, 0x80, 0x80));
    canvas.clear();
    canvas.present();

    'main: loop {
        let event = event_pump.wait_event();
        match event {
            Event::Quit { .. } => break,

            Event::KeyDown {
                scancode: Some(sc),
                repeat,
                keymod,
                ..
            } => {
                if repeat {
                    continue;
                }
                if sc == Scancode::Q && keymod.contains(Mod::RCTRLMOD) {
                    break 'main;
                }
                let raw = sc as u32;
                if let Some(consumer_code) = keymap::scancode_to_consumer(raw) {
                    sender.send_consumer(true, consumer_code);
                } else if let Ok(sc8) = u8::try_from(raw) {
                    sender.send_keyboard(true, sc8);
                }
            }

            Event::KeyUp {
                scancode: Some(sc),
                repeat,
                ..
            } => {
                if repeat {
                    continue;
                }
                let raw = sc as u32;
                if let Some(consumer_code) = keymap::scancode_to_consumer(raw) {
                    sender.send_consumer(false, consumer_code);
                } else if let Ok(sc8) = u8::try_from(raw) {
                    sender.send_keyboard(false, sc8);
                }
            }

            Event::MouseButtonDown { mouse_btn, .. } => {
                match mouse_btn {
                    MouseButton::Left => mouse_buttons |= 0x01,
                    MouseButton::Right => mouse_buttons |= 0x02,
                    MouseButton::Middle => mouse_buttons |= 0x04,
                    _ => {}
                }
                sender.send_mouse(mouse_buttons, 0, 0, 0, 0);
            }

            Event::MouseButtonUp { mouse_btn, .. } => {
                match mouse_btn {
                    MouseButton::Left => mouse_buttons &= !0x01,
                    MouseButton::Right => mouse_buttons &= !0x02,
                    MouseButton::Middle => mouse_buttons &= !0x04,
                    _ => {}
                }
                sender.send_mouse(mouse_buttons, 0, 0, 0, 0);
            }

            Event::MouseMotion { x, y, .. } => {
                let dx = x - 320;
                let dy = y - 240;
                if dx != 0 || dy != 0 {
                    sender.send_mouse(mouse_buttons, dx, dy, 0, 0);
                    sdl.mouse()
                        .warp_mouse_in_window(canvas.window(), 320, 240);
                }
            }

            Event::MouseWheel { x, y, .. } => {
                if x != 0 || y != 0 {
                    sender.send_mouse(mouse_buttons, 0, 0, y, x);
                }
            }

            _ => {}
        }
    }

    // Release exit combo keys and any mouse buttons so the device doesn't get stuck
    sender.send_keyboard(false, Scancode::RCtrl as u8);
    sender.send_keyboard(false, Scancode::Q as u8);
    if mouse_buttons != 0 {
        sender.send_mouse(0, 0, 0, 0, 0);
    }

    println!("Exiting.");
}
