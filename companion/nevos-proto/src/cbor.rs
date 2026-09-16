//! A minimal canonical CBOR writer and reader (RFC 8949).
//!
//! Hand-written, and a deliberate mirror of `components/nev_bridge/src/nev_cbor.c`.
//! The same subset on both sides: unsigned and negative integers, byte and text
//! strings, definite-length arrays, booleans and 32-bit floats.
//!
//! Refusing indefinite lengths, maps and tags is a security property rather than
//! laziness — the firmware's decoder runs on data from the network with 512 KB of
//! RAM behind it, and the two sides must agree on exactly what is legal or the
//! daemon will happily send something the device cannot parse.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProtoError {
    /// Ran off the end of the buffer.
    Truncated,
    /// Wrong major type, or a construct outside the supported subset.
    Malformed,
    /// A value too wide for the field it was being read into.
    OutOfRange,
    /// A string or byte field longer than the schema allows.
    TooLong,
    /// The payload is a valid message, but not the one that was asked for.
    WrongMessage,
}

impl std::fmt::Display for ProtoError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            ProtoError::Truncated => "truncated",
            ProtoError::Malformed => "malformed",
            ProtoError::OutOfRange => "value out of range",
            ProtoError::TooLong => "longer than the schema allows",
            ProtoError::WrongMessage => "wrong message type",
        };
        f.write_str(s)
    }
}

impl std::error::Error for ProtoError {}

const MT_UINT: u8 = 0;
const MT_NINT: u8 = 1;
const MT_BYTES: u8 = 2;
const MT_TEXT: u8 = 3;
const MT_ARRAY: u8 = 4;
const MT_SIMPLE: u8 = 7;

// ------------------------------------------------------------------- writer

#[derive(Default)]
pub struct CborWriter {
    buf: Vec<u8>,
}

impl CborWriter {
    pub fn new() -> Self {
        Self { buf: Vec::new() }
    }

    /// Head byte plus the shortest argument that holds `v`. Canonical CBOR has
    /// exactly one encoding per value; anything else breaks the golden vectors.
    fn head(&mut self, major: u8, v: u64) {
        let m = major << 5;
        if v < 24 {
            self.buf.push(m | v as u8);
        } else if v <= u8::MAX as u64 {
            self.buf.push(m | 24);
            self.buf.push(v as u8);
        } else if v <= u16::MAX as u64 {
            self.buf.push(m | 25);
            self.buf.extend_from_slice(&(v as u16).to_be_bytes());
        } else if v <= u32::MAX as u64 {
            self.buf.push(m | 26);
            self.buf.extend_from_slice(&(v as u32).to_be_bytes());
        } else {
            self.buf.push(m | 27);
            self.buf.extend_from_slice(&v.to_be_bytes());
        }
    }

    pub fn u64(&mut self, v: u64) {
        self.head(MT_UINT, v);
    }

    pub fn i64(&mut self, v: i64) {
        if v < 0 {
            // CBOR stores -1-n. Computed on the unsigned side because negating
            // i64::MIN overflows.
            self.head(MT_NINT, (-(v + 1)) as u64);
        } else {
            self.head(MT_UINT, v as u64);
        }
    }

    pub fn bool(&mut self, v: bool) {
        self.buf.push((MT_SIMPLE << 5) | if v { 21 } else { 20 });
    }

    pub fn f32(&mut self, v: f32) {
        self.buf.push((MT_SIMPLE << 5) | 26);
        self.buf.extend_from_slice(&v.to_bits().to_be_bytes());
    }

    pub fn bytes(&mut self, v: &[u8]) {
        self.head(MT_BYTES, v.len() as u64);
        self.buf.extend_from_slice(v);
    }

    pub fn text(&mut self, v: &str) {
        let raw = v.as_bytes();
        self.head(MT_TEXT, raw.len() as u64);
        self.buf.extend_from_slice(raw);
    }

    pub fn array(&mut self, count: usize) {
        self.head(MT_ARRAY, count as u64);
    }

    pub fn finish(self) -> Vec<u8> {
        self.buf
    }
}

// ------------------------------------------------------------------- reader

pub struct CborReader<'a> {
    buf: &'a [u8],
    pos: usize,
}

impl<'a> CborReader<'a> {
    pub fn new(buf: &'a [u8]) -> Self {
        Self { buf, pos: 0 }
    }

    pub fn position(&self) -> usize {
        self.pos
    }

    pub fn is_done(&self) -> bool {
        self.pos >= self.buf.len()
    }

    fn take(&mut self, n: usize) -> Result<&'a [u8], ProtoError> {
        if self.buf.len() - self.pos < n {
            return Err(ProtoError::Truncated);
        }
        let s = &self.buf[self.pos..self.pos + n];
        self.pos += n;
        Ok(s)
    }

    fn head(&mut self) -> Result<(u8, u64), ProtoError> {
        let b = *self.take(1)?.first().ok_or(ProtoError::Truncated)?;
        let major = b >> 5;
        let ai = b & 0x1F;

        if ai < 24 {
            return Ok((major, ai as u64));
        }
        let n = match ai {
            24 => 1,
            25 => 2,
            26 => 4,
            27 => 8,
            // 28-30 reserved, 31 indefinite. Both refused: indefinite lengths
            // are how a decoder is made to loop on attacker-controlled input.
            _ => return Err(ProtoError::Malformed),
        };
        let mut v: u64 = 0;
        for byte in self.take(n)? {
            v = (v << 8) | *byte as u64;
        }
        Ok((major, v))
    }

    pub fn u64(&mut self) -> Result<u64, ProtoError> {
        let start = self.pos;
        match self.head()? {
            (MT_UINT, v) => Ok(v),
            _ => {
                self.pos = start;
                Err(ProtoError::Malformed)
            }
        }
    }

    pub fn i64(&mut self) -> Result<i64, ProtoError> {
        let start = self.pos;
        match self.head()? {
            (MT_UINT, v) if v <= i64::MAX as u64 => Ok(v as i64),
            (MT_NINT, v) if v <= i64::MAX as u64 => Ok(-(v as i64) - 1),
            _ => {
                self.pos = start;
                Err(ProtoError::OutOfRange)
            }
        }
    }

    pub fn bool(&mut self) -> Result<bool, ProtoError> {
        let start = self.pos;
        let b = *self.take(1)?.first().ok_or(ProtoError::Truncated)?;
        match b {
            0xF5 => Ok(true),
            0xF4 => Ok(false),
            _ => {
                self.pos = start;
                Err(ProtoError::Malformed)
            }
        }
    }

    pub fn f32(&mut self) -> Result<f32, ProtoError> {
        let start = self.pos;
        let b = *self.take(1)?.first().ok_or(ProtoError::Truncated)?;
        if b != ((MT_SIMPLE << 5) | 26) {
            self.pos = start;
            return Err(ProtoError::Malformed);
        }
        let raw = self.take(4)?;
        Ok(f32::from_bits(u32::from_be_bytes([
            raw[0], raw[1], raw[2], raw[3],
        ])))
    }

    fn string(&mut self, want: u8, max: usize) -> Result<&'a [u8], ProtoError> {
        let start = self.pos;
        let (major, len) = self.head()?;
        if major != want {
            self.pos = start;
            return Err(ProtoError::Malformed);
        }
        // Checked before it is trusted: a header claiming four gigabytes is the
        // classic way to walk a parser off the end of its buffer.
        if len > (self.buf.len() - self.pos) as u64 {
            self.pos = start;
            return Err(ProtoError::Truncated);
        }
        if len > max as u64 {
            self.pos = start;
            return Err(ProtoError::TooLong);
        }
        self.take(len as usize)
    }

    pub fn text(&mut self, max: usize) -> Result<String, ProtoError> {
        let raw = self.string(MT_TEXT, max)?;
        String::from_utf8(raw.to_vec()).map_err(|_| ProtoError::Malformed)
    }

    pub fn bytes(&mut self, max: usize) -> Result<Vec<u8>, ProtoError> {
        Ok(self.string(MT_BYTES, max)?.to_vec())
    }

    pub fn array(&mut self) -> Result<usize, ProtoError> {
        let start = self.pos;
        let (major, len) = self.head()?;
        if major != MT_ARRAY {
            self.pos = start;
            return Err(ProtoError::Malformed);
        }
        // An array cannot promise more elements than there are bytes left, since
        // the shortest possible element is one byte.
        if len > (self.buf.len() - self.pos) as u64 {
            self.pos = start;
            return Err(ProtoError::Truncated);
        }
        Ok(len as usize)
    }

    /// Steps over the next item, so fields appended by a newer peer can be
    /// ignored. One level only: the protocol never nests, so a nested array is
    /// either a bug or an attempt to make this recurse.
    pub fn skip(&mut self) -> Result<(), ProtoError> {
        let (major, arg) = self.head()?;
        match major {
            MT_UINT | MT_NINT | MT_SIMPLE => Ok(()),
            MT_BYTES | MT_TEXT => {
                if arg > (self.buf.len() - self.pos) as u64 {
                    return Err(ProtoError::Truncated);
                }
                self.pos += arg as usize;
                Ok(())
            }
            MT_ARRAY => {
                for _ in 0..arg {
                    let (m, a) = self.head()?;
                    if m == MT_ARRAY {
                        return Err(ProtoError::Malformed);
                    }
                    if m == MT_BYTES || m == MT_TEXT {
                        if a > (self.buf.len() - self.pos) as u64 {
                            return Err(ProtoError::Truncated);
                        }
                        self.pos += a as usize;
                    }
                }
                Ok(())
            }
            _ => Err(ProtoError::Malformed),
        }
    }
}
