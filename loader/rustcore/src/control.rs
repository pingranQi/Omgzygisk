//! Decoder for the monitor's Unix datagram control protocol.
//!
//! The platform adapter supplies one complete datagram. This module validates
//! the packet before a caller acts on it.

pub const MAX_PAYLOAD_BYTES: usize = 64 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Command {
    Start = 1,
    Stop = 2,
    Exit = 3,
    ZygoteInjected = 4,
    DaemonSetInfo = 5,
    DaemonSetErrorInfo = 6,
    SystemServerStarted = 7,
}

impl TryFrom<u32> for Command {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Self::Start),
            2 => Ok(Self::Stop),
            3 => Ok(Self::Exit),
            4 => Ok(Self::ZygoteInjected),
            5 => Ok(Self::DaemonSetInfo),
            6 => Ok(Self::DaemonSetErrorInfo),
            7 => Ok(Self::SystemServerStarted),
            _ => Err(Error::InvalidCommand),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    UnexpectedEnd,
    InvalidCommand,
    InvalidLength,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Packet<'a> {
    pub command: Command,
    pub payload: &'a [u8],
}

/// Decode a native-layout command packet. Commands carrying status text append
/// a signed 32-bit little-endian length followed by exactly that many bytes.
pub fn decode(bytes: &[u8]) -> Result<Packet<'_>, Error> {
    let command_bytes: [u8; 4] = bytes
        .get(..4)
        .ok_or(Error::UnexpectedEnd)?
        .try_into()
        .map_err(|_| Error::UnexpectedEnd)?;
    let command = Command::try_from(u32::from_le_bytes(command_bytes))?;

    let has_payload = matches!(
        command,
        Command::DaemonSetInfo | Command::DaemonSetErrorInfo
    );

    if !has_payload {
        if bytes.len() != 4 {
            return Err(Error::InvalidLength);
        }
        return Ok(Packet {
            command,
            payload: &bytes[4..],
        });
    }

    let length_bytes: [u8; 4] = bytes
        .get(4..8)
        .ok_or(Error::UnexpectedEnd)?
        .try_into()
        .map_err(|_| Error::UnexpectedEnd)?;
    let signed_length = i32::from_le_bytes(length_bytes);

    if signed_length < 0 || signed_length as usize > MAX_PAYLOAD_BYTES {
        return Err(Error::InvalidLength);
    }

    let payload_length = signed_length as usize;
    let expected_length = 8usize
        .checked_add(payload_length)
        .ok_or(Error::InvalidLength)?;

    if bytes.len() != expected_length {
        return Err(Error::InvalidLength);
    }

    Ok(Packet {
        command,
        payload: &bytes[8..],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_short_command() {
        let packet = decode(&[1, 0, 0, 0]).expect("valid start packet");
        assert_eq!(packet.command, Command::Start);
        assert!(packet.payload.is_empty());
    }

    #[test]
    fn accepts_bounded_status_payload() {
        let packet =
            decode(&[5, 0, 0, 0, 3, 0, 0, 0, b'o', b'k', 0]).expect("valid status packet");
        assert_eq!(packet.command, Command::DaemonSetInfo);
        assert_eq!(packet.payload, b"ok\0");
    }

    #[test]
    fn rejects_malformed_packets() {
        assert_eq!(decode(&[99, 0, 0, 0]), Err(Error::InvalidCommand));
        assert_eq!(decode(&[1, 0, 0, 0, 0]), Err(Error::InvalidLength));
        assert_eq!(
            decode(&[5, 0, 0, 0, 255, 255, 255, 127]),
            Err(Error::InvalidLength)
        );
        assert_eq!(decode(&[5, 0, 0, 0]), Err(Error::UnexpectedEnd));
    }
}
