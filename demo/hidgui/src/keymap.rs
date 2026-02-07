// SDL media scancode -> HID consumer code mapping

pub const fn scancode_to_consumer(raw: u32) -> Option<u16> {
    match raw {
        128 => Some(0x00E9), // VolumeUp
        129 => Some(0x00EA), // VolumeDown
        258 => Some(0x00B5), // AudioNext
        259 => Some(0x00B6), // AudioPrev
        260 => Some(0x00B7), // AudioStop
        261 => Some(0x00CD), // AudioPlay
        262 => Some(0x00E2), // AudioMute
        _ => None,
    }
}
