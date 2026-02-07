// UDP socket, v1/v2 packet construction and sending

use std::net::UdpSocket;

const PORT: u16 = 4444;
const PACKET_TYPE_KEYBOARD: u8 = 0x01;
const PACKET_TYPE_MOUSE: u8 = 0x02;
const PACKET_TYPE_CONSUMER: u8 = 0x03;

pub struct UdpSender {
    socket: UdpSocket,
    token: Option<[u8; 16]>,
}

impl UdpSender {
    pub fn new(host: &str, token: Option<[u8; 16]>) -> Self {
        let socket = UdpSocket::bind("0.0.0.0:0").expect("failed to bind UDP socket");
        let addr = format!("{host}:{PORT}");
        socket.connect(&addr).expect("failed to connect UDP socket");
        Self { socket, token }
    }

    fn build_header(&self, pkt_type: u8) -> Vec<u8> {
        if let Some(token) = &self.token {
            let mut hdr = Vec::with_capacity(18);
            hdr.push(pkt_type);
            hdr.push(0x02);
            hdr.extend_from_slice(token);
            hdr
        } else {
            vec![pkt_type, 0x01]
        }
    }

    pub fn send_keyboard(&self, pressed: bool, scancode: u8) {
        let mut pkt = self.build_header(PACKET_TYPE_KEYBOARD);
        pkt.push(u8::from(pressed));
        pkt.push(0); // modifiers
        pkt.push(scancode);
        let _ = self.socket.send(&pkt);
    }

    #[allow(clippy::cast_sign_loss)]
    pub fn send_mouse(&self, buttons: u8, mut x: i32, mut y: i32, mut vert: i32, mut horiz: i32) {
        loop {
            let cx = clamp8(x);
            let cy = clamp8(y);
            let cv = clamp8(vert);
            let ch = clamp8(horiz);

            let mut pkt = self.build_header(PACKET_TYPE_MOUSE);
            pkt.push(buttons);
            pkt.push(cx as u8);
            pkt.push(cy as u8);
            pkt.push(cv as u8);
            pkt.push(ch as u8);
            let _ = self.socket.send(&pkt);

            x -= i32::from(cx);
            y -= i32::from(cy);
            vert -= i32::from(cv);
            horiz -= i32::from(ch);

            if x == 0 && y == 0 && vert == 0 && horiz == 0 {
                break;
            }
        }
    }

    pub fn send_consumer(&self, pressed: bool, code: u16) {
        let mut pkt = self.build_header(PACKET_TYPE_CONSUMER);
        pkt.push(u8::from(pressed));
        pkt.extend_from_slice(&code.to_le_bytes());
        let _ = self.socket.send(&pkt);
    }
}

#[allow(clippy::cast_possible_truncation)]
fn clamp8(v: i32) -> i8 {
    v.clamp(-127, 127) as i8
}
