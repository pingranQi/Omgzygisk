//! Bounded primitives for the daemon's typed-field IPC stream.
//!
//! These helpers perform no I/O. Strings are represented as borrowed byte
//! slices so callers can validate framing without allocating.

pub const MAX_STRING_BYTES: usize = 1024 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    UnexpectedEnd,
    InvalidLength,
    LengthOverflow,
    OutOfMemory,
}

/// Wire values from zygiskd/src/constants.rs. New values may only be
/// appended because these discriminants are part of the daemon protocol.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DaemonSocketAction {
    PingHeartbeat = 0,
    GetProcessFlags = 1,
    CacheMountNamespace = 2,
    UpdateMountNamespace = 3,
    ReadModules = 4,
    RequestCompanionSocket = 5,
    GetModuleDir = 6,
    ZygoteRestart = 7,
    SystemServerStarted = 8,
    ListFnNodes = 9,
    SetFnNodeEnabled = 10,
    RemoveFnNode = 11,
    ReadFnModules = 12,
    GetFnModuleDir = 13,
}

impl TryFrom<u8> for DaemonSocketAction {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::PingHeartbeat),
            1 => Ok(Self::GetProcessFlags),
            2 => Ok(Self::CacheMountNamespace),
            3 => Ok(Self::UpdateMountNamespace),
            4 => Ok(Self::ReadModules),
            5 => Ok(Self::RequestCompanionSocket),
            6 => Ok(Self::GetModuleDir),
            7 => Ok(Self::ZygoteRestart),
            8 => Ok(Self::SystemServerStarted),
            9 => Ok(Self::ListFnNodes),
            10 => Ok(Self::SetFnNodeEnabled),
            11 => Ok(Self::RemoveFnNode),
            12 => Ok(Self::ReadFnModules),
            13 => Ok(Self::GetFnModuleDir),
            _ => Err(Error::InvalidLength),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FnModule<'a> {
    pub id: &'a [u8],
    pub triggers: &'a [u8],
    pub scope: u8,
    pub apps: &'a [u8],
    pub priority: u32,
}

pub struct Reader<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Reader<'a> {
    pub fn init(bytes: &'a [u8]) -> Self {
        Self { bytes, offset: 0 }
    }

    pub fn remaining(&self) -> usize {
        self.bytes.len() - self.offset
    }

    fn take(&mut self, length: usize) -> Result<&'a [u8], Error> {
        let end = self
            .offset
            .checked_add(length)
            .ok_or(Error::InvalidLength)?;
        let result = self
            .bytes
            .get(self.offset..end)
            .ok_or(Error::UnexpectedEnd)?;
        self.offset = end;
        Ok(result)
    }

    pub fn read_u8(&mut self) -> Result<u8, Error> {
        Ok(self.take(1)?[0])
    }

    pub fn read_u32(&mut self) -> Result<u32, Error> {
        let bytes: [u8; 4] = self
            .take(4)?
            .try_into()
            .map_err(|_| Error::UnexpectedEnd)?;
        Ok(u32::from_le_bytes(bytes))
    }

    pub fn read_usize(&mut self) -> Result<usize, Error> {
        if usize::BITS == 32 {
            return self.read_u32().map(|value| value as usize);
        }

        let value = self.read_u64()?;
        usize::try_from(value).map_err(|_| Error::InvalidLength)
    }

    fn read_u64(&mut self) -> Result<u64, Error> {
        let bytes: [u8; 8] = self
            .take(8)?
            .try_into()
            .map_err(|_| Error::UnexpectedEnd)?;
        Ok(u64::from_le_bytes(bytes))
    }

    /// Read a length-prefixed byte string, rejecting oversized values before
    /// slicing. Text/UTF-8 validation belongs to the caller.
    pub fn read_string(&mut self) -> Result<&'a [u8], Error> {
        let length = self.read_usize()?;
        if length > MAX_STRING_BYTES {
            return Err(Error::InvalidLength);
        }
        self.take(length)
    }

    /// Read one FN metadata record. Its descriptor is transported separately
    /// with SCM_RIGHTS and is deliberately not represented here.
    pub fn read_fn_module(&mut self) -> Result<FnModule<'a>, Error> {
        let id = self.read_string()?;
        let triggers = self.read_string()?;
        let scope = self.read_u8()?;
        if scope > 2 {
            return Err(Error::InvalidLength);
        }
        let apps = self.read_string()?;
        let priority = self.read_u32()?;

        Ok(FnModule {
            id,
            triggers,
            scope,
            apps,
            priority,
        })
    }
}

#[derive(Default)]
pub struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn as_slice(&self) -> &[u8] {
        &self.bytes
    }

    /// Clear the current message while retaining its allocation for reuse.
    pub fn clear(&mut self) {
        self.bytes.clear();
    }

    fn append(&mut self, bytes: &[u8]) -> Result<(), Error> {
        self.bytes
            .try_reserve(bytes.len())
            .map_err(|_| Error::OutOfMemory)?;
        self.bytes.extend_from_slice(bytes);
        Ok(())
    }

    pub fn write_u8(&mut self, value: u8) -> Result<(), Error> {
        self.append(&[value])
    }

    pub fn write_u32(&mut self, value: u32) -> Result<(), Error> {
        self.append(&value.to_le_bytes())
    }

    pub fn write_usize(&mut self, value: usize) -> Result<(), Error> {
        if usize::BITS == 32 {
            let value = u32::try_from(value).map_err(|_| Error::LengthOverflow)?;
            return self.write_u32(value);
        }

        let value = u64::try_from(value).map_err(|_| Error::LengthOverflow)?;
        self.append(&value.to_le_bytes())
    }

    pub fn write_string(&mut self, value: &[u8]) -> Result<(), Error> {
        if value.len() > MAX_STRING_BYTES {
            return Err(Error::InvalidLength);
        }

        // Reserve the length prefix and payload together. This avoids a
        // second growth/reallocation when a fresh writer serializes a string.
        let mut prefix = [0u8; 8];
        let prefix_len = if usize::BITS == 32 {
            let length = u32::try_from(value.len()).map_err(|_| Error::LengthOverflow)?;
            prefix[..4].copy_from_slice(&length.to_le_bytes());
            4
        } else {
            let length = u64::try_from(value.len()).map_err(|_| Error::LengthOverflow)?;
            prefix.copy_from_slice(&length.to_le_bytes());
            8
        };

        let additional = prefix_len
            .checked_add(value.len())
            .ok_or(Error::LengthOverflow)?;
        self.bytes
            .try_reserve(additional)
            .map_err(|_| Error::OutOfMemory)?;
        self.bytes.extend_from_slice(&prefix[..prefix_len]);
        self.bytes.extend_from_slice(value);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reader_decodes_bounded_little_endian_fields() {
        let mut writer = Writer::new();
        writer.write_u8(7).unwrap();
        writer.write_u32(0x1234_5678).unwrap();
        writer.write_string(b"zygisk").unwrap();

        let mut reader = Reader::init(writer.as_slice());
        assert_eq!(reader.read_u8(), Ok(7));
        assert_eq!(reader.read_u32(), Ok(0x1234_5678));
        assert_eq!(reader.read_string(), Ok(&b"zygisk"[..]));
        assert_eq!(reader.remaining(), 0);
    }

    #[test]
    fn reader_rejects_truncated_fields() {
        let mut reader = Reader::init(&[1, 2, 3]);
        assert_eq!(reader.read_u32(), Err(Error::UnexpectedEnd));
    }

    #[test]
    fn reader_rejects_oversized_strings_before_slicing() {
        let oversized = (MAX_STRING_BYTES as u64 + 1).to_le_bytes();
        let mut reader = Reader::init(&oversized);
        assert_eq!(reader.read_string(), Err(Error::InvalidLength));
    }

    #[test]
    fn writer_reuses_capacity_after_clear() {
        let mut writer = Writer::new();
        writer.write_string(&vec![b'x'; 4096]).unwrap();
        let capacity = writer.bytes.capacity();
        assert!(capacity >= 4096);

        writer.clear();
        assert!(writer.as_slice().is_empty());
        writer.write_string(b"reused").unwrap();
        assert_eq!(writer.bytes.capacity(), capacity);
        assert_eq!(writer.as_slice().last(), Some(&b'd'));
    }

    #[test]
    fn writer_rejects_oversized_strings() {
        let mut writer = Writer::new();
        let value = vec![0u8; MAX_STRING_BYTES + 1];
        assert_eq!(writer.write_string(&value), Err(Error::InvalidLength));
    }

    #[test]
    fn reader_preserves_trailing_fields_for_the_next_layer() {
        let mut reader = Reader::init(&[12, 34]);
        assert_eq!(reader.read_u8(), Ok(12));
        assert_eq!(reader.remaining(), 1);
        assert_eq!(reader.read_u8(), Ok(34));
    }

    #[test]
    fn daemon_action_values_remain_wire_compatible() {
        assert_eq!(
            DaemonSocketAction::try_from(0),
            Ok(DaemonSocketAction::PingHeartbeat)
        );
        assert_eq!(
            DaemonSocketAction::try_from(12),
            Ok(DaemonSocketAction::ReadFnModules)
        );
        assert_eq!(
            DaemonSocketAction::try_from(14),
            Err(Error::InvalidLength)
        );
    }

    #[test]
    fn reader_decodes_fn_metadata_and_rejects_invalid_scope() {
        let mut writer = Writer::new();
        writer.write_string(b"example").unwrap();
        writer.write_string(b"app,zygote").unwrap();
        writer.write_u8(1).unwrap();
        writer.write_string(b"com.example").unwrap();
        writer.write_u32(42).unwrap();

        let mut reader = Reader::init(writer.as_slice());
        let module = reader.read_fn_module().unwrap();
        assert_eq!(module.id, b"example");
        assert_eq!(module.triggers, b"app,zygote");
        assert_eq!(module.scope, 1);
        assert_eq!(module.apps, b"com.example");
        assert_eq!(module.priority, 42);
        assert_eq!(reader.remaining(), 0);

        let mut invalid = Writer::new();
        invalid.write_string(b"bad").unwrap();
        invalid.write_string(b"app").unwrap();
        invalid.write_u8(3).unwrap();
        let mut reader = Reader::init(invalid.as_slice());
        assert_eq!(reader.read_fn_module(), Err(Error::InvalidLength));
    }
}
