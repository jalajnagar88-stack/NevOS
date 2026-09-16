#!/usr/bin/env python3
"""Generate the NEVOS bridge codec for C and Rust from schema/nevos.toml.

Neither side of the wire is hand-written, so the two cannot drift: a field added
to the schema that one side forgets to handle is a build failure rather than a
corrupt value found in production.

Outputs:
  components/nev_bridge/include/nev_bridge/nev_proto.h
  components/nev_bridge/src/nev_proto.c
  components/nev_bridge/test/golden_data.h          C test fixtures
  companion/nevos-proto/src/generated.rs
  companion/nevos-proto/src/golden.rs               Rust test fixtures
  schema/golden/*.cbor                              canonical bytes

The golden vectors are encoded here, in Python, by a third independent
implementation of canonical CBOR. Both the C and the Rust codecs are then
checked against them, so an encoding disagreement between any two of the three
is caught rather than shipped.

    tools/schema/gen.py [--check]

--check regenerates into memory and fails if the checked-in files differ.
"""
import argparse
import pathlib
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "schema" / "nevos.toml"

BANNER = "GENERATED FROM schema/nevos.toml BY tools/schema/gen.py — DO NOT EDIT"

# ---------------------------------------------------------------- CBOR (reference)


def cbor_head(major: int, value: int) -> bytes:
    """Canonical: the shortest argument that holds `value`."""
    m = major << 5
    if value < 24:
        return bytes([m | value])
    if value <= 0xFF:
        return bytes([m | 24, value])
    if value <= 0xFFFF:
        return bytes([m | 25]) + value.to_bytes(2, "big")
    if value <= 0xFFFFFFFF:
        return bytes([m | 26]) + value.to_bytes(4, "big")
    return bytes([m | 27]) + value.to_bytes(8, "big")


def cbor_uint(v):
    return cbor_head(0, v)


def cbor_text(s):
    raw = s.encode("utf-8")
    return cbor_head(3, len(raw)) + raw


def cbor_bytes(b):
    return cbor_head(2, len(b)) + b


def cbor_bool(v):
    return bytes([0xF5 if v else 0xF4])


def cbor_f32(v):
    import struct

    return bytes([0xFA]) + struct.pack(">f", v)


def cbor_array(n):
    return cbor_head(4, n)


# ------------------------------------------------------------------- type maps

C_SCALAR = {
    "u8": ("uint8_t", "u32", "0xFFu"),
    "u16": ("uint16_t", "u32", "0xFFFFu"),
    "u32": ("uint32_t", "u32", None),
    "u64": ("uint64_t", "u64", None),
    "i32": ("int32_t", "i32", None),
}
RUST_SCALAR = {
    "u8": "u8",
    "u16": "u16",
    "u32": "u32",
    "u64": "u64",
    "i32": "i32",
    "bool": "bool",
    "f32": "f32",
}


def c_type(f):
    t = f["type"]
    if t in C_SCALAR:
        return C_SCALAR[t][0]
    if t == "bool":
        return "bool"
    if t == "f32":
        return "float"
    raise SystemExit(f"unknown type {t}")


def sample_value(field, index):
    """Deterministic per field, so all three implementations agree."""
    t = field["type"]
    if t == "bool":
        return index % 2 == 0
    if t == "f32":
        return float(index) + 0.5
    if t == "str":
        base = f"{field['name']}-{index}"
        return base[: field["max"]]
    if t == "bytes":
        n = min(field["max"], 8)
        return bytes((index * 17 + i) & 0xFF for i in range(n))
    limits = {"u8": 0xFF, "u16": 0xFFFF, "u32": 0xFFFFFFFF, "u64": 0xFFFFFFFFFF, "i32": 0x7FFFFFFF}
    return (index + 1) * 7 % limits[t]


def encode_sample(msg):
    fields = msg.get("field", [])
    out = cbor_array(1 + len(fields)) + cbor_uint(msg["id"])
    for i, f in enumerate(fields):
        v = sample_value(f, i)
        t = f["type"]
        if t == "bool":
            out += cbor_bool(v)
        elif t == "f32":
            out += cbor_f32(v)
        elif t == "str":
            out += cbor_text(v)
        elif t == "bytes":
            out += cbor_bytes(v)
        elif t == "i32":
            out += cbor_uint(v)  # samples are non-negative
        else:
            out += cbor_uint(v)
    return out


# ------------------------------------------------------------------------ C


def gen_c_header(spec):
    L = [
        "/*",
        f" * {BANNER}",
        " *",
        " * The NEVOS bridge wire protocol. See docs/protocol.md and schema/nevos.toml.",
        " *",
        " * Strings decode into fixed arrays sized by the schema. Byte fields decode to a",
        " * BORROWED pointer into the caller's frame buffer — nothing is copied and nothing",
        " * is allocated, so the data is valid only while that buffer is.",
        " */",
        "#ifndef NEV_BRIDGE_NEV_PROTO_H",
        "#define NEV_BRIDGE_NEV_PROTO_H",
        "",
        '#include "nev_port/nev_types.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f"#define NEV_PROTO_VERSION {spec['protocol_version']}u",
        f"#define NEV_PROTO_MAX_FRAME {spec['max_frame_bytes']}u",
        f"#define NEV_PROTO_FRAME_HEADER 4u  /* u32 big-endian length prefix */",
        "",
        "typedef enum {",
    ]
    for m in spec["message"]:
        L.append(f"    NEV_MSG_{m['name'].upper()} = {m['id']},")
    L += ["} nev_msg_id_t;", "", "const char *nev_proto_name(uint16_t id);", ""]

    for m in spec["message"]:
        doc = (m.get("doc") or "").strip()
        if doc:
            L.append("/* " + doc.replace("\n", "\n * ") + " */")
        L.append("typedef struct {")
        for f in m.get("field", []):
            fdoc = (f.get("doc") or "").strip()
            if fdoc:
                L.append(f"    /* {fdoc} */")
            if f["type"] == "str":
                L.append(f"    char {f['name']}[{f['max'] + 1}];")
            elif f["type"] == "bytes":
                L.append(f"    const uint8_t *{f['name']}; /* borrowed */")
                L.append(f"    size_t {f['name']}_len;")
            else:
                L.append(f"    {c_type(f)} {f['name']};")
        if not m.get("field"):
            L.append("    uint8_t _empty; /* C forbids an empty struct */")
        L.append(f"}} nev_msg_{m['name']}_t;")
        L.append("")

    L += [
        "/* Encodes the payload (no length prefix). NEV_ERR_NO_SPACE if it will not fit. */",
    ]
    for m in spec["message"]:
        n = m["name"]
        L.append(
            f"nev_err_t nev_proto_encode_{n}(const nev_msg_{n}_t *msg, uint8_t *buf, size_t cap, size_t *out_len);"
        )
    L.append("")
    L.append("/* Decodes a payload. Trailing fields from a newer peer are ignored; missing")
    L.append(" * trailing fields are left at zero. See the compatibility rules in the schema. */")
    for m in spec["message"]:
        n = m["name"]
        L.append(
            f"nev_err_t nev_proto_decode_{n}(const uint8_t *buf, size_t len, nev_msg_{n}_t *out);"
        )
    L += [
        "",
        "/* Reads the message id without decoding the body, so a receiver can dispatch. */",
        "nev_err_t nev_proto_peek_id(const uint8_t *buf, size_t len, uint16_t *out_id);",
        "",
        "/* Length-prefixed framing. */",
        "nev_err_t nev_proto_frame_wrap(const uint8_t *payload, size_t payload_len, uint8_t *buf,",
        "                               size_t cap, size_t *out_len);",
        "/* Reports the payload span of the first complete frame in `buf`, and how many",
        " * bytes that frame occupied. NEV_ERR_TIMEOUT means more data is needed. */",
        "nev_err_t nev_proto_frame_split(const uint8_t *buf, size_t len, const uint8_t **payload,",
        "                                size_t *payload_len, size_t *frame_len);",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "#endif /* NEV_BRIDGE_NEV_PROTO_H */",
        "",
    ]
    return "\n".join(L)


def gen_c_source(spec):
    L = [
        f"/* {BANNER} */",
        '#include "nev_bridge/nev_proto.h"',
        '#include "nev_bridge/nev_cbor.h"',
        "#include <string.h>",
        "",
        "const char *nev_proto_name(uint16_t id) {",
        "    switch (id) {",
    ]
    for m in spec["message"]:
        L.append(f"        case NEV_MSG_{m['name'].upper()}: return \"{m['name']}\";")
    L += ["        default: return \"?\";", "    }", "}", ""]

    for m in spec["message"]:
        n = m["name"]
        fields = m.get("field", [])
        L += [
            f"nev_err_t nev_proto_encode_{n}(const nev_msg_{n}_t *msg, uint8_t *buf, size_t cap, size_t *out_len) {{",
            "    if (!msg || !buf) return NEV_ERR_INVALID_ARG;",
            "    nev_cbor_w_t w;",
            "    nev_cbor_w_init(&w, buf, cap);",
            f"    nev_cbor_w_array(&w, {len(fields) + 1});",
            f"    nev_cbor_w_u64(&w, NEV_MSG_{n.upper()});",
        ]
        for f in fields:
            t, name = f["type"], f["name"]
            if t == "bool":
                L.append(f"    nev_cbor_w_bool(&w, msg->{name});")
            elif t == "f32":
                L.append(f"    nev_cbor_w_f32(&w, msg->{name});")
            elif t == "str":
                L.append(f"    nev_cbor_w_text(&w, msg->{name});")
            elif t == "bytes":
                L.append(f"    if (msg->{name}_len > {f['max']}u) return NEV_ERR_NO_SPACE;")
                L.append(f"    nev_cbor_w_bytes(&w, msg->{name}, msg->{name}_len);")
            elif t == "i32":
                L.append(f"    nev_cbor_w_i64(&w, msg->{name});")
            else:
                L.append(f"    nev_cbor_w_u64(&w, msg->{name});")
        L += [
            "    if (w.overflow) return NEV_ERR_NO_SPACE;",
            "    if (out_len) *out_len = w.len;",
            "    return NEV_OK;",
            "}",
            "",
            f"nev_err_t nev_proto_decode_{n}(const uint8_t *buf, size_t len, nev_msg_{n}_t *out) {{",
            "    if (!buf || !out) return NEV_ERR_INVALID_ARG;",
            "    memset(out, 0, sizeof(*out));",
            "    nev_cbor_r_t r;",
            "    nev_cbor_r_init(&r, buf, len);",
            "    size_t count = 0;",
            "    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;",
            "    uint32_t id = 0;",
            "    if (!nev_cbor_r_u32(&r, &id)) return NEV_ERR_INVALID_ARG;",
            f"    if (id != NEV_MSG_{n.upper()}) return NEV_ERR_INVALID_ARG;",
            "    const size_t present = count - 1;",
        ]
        if fields:
            L.append("    uint32_t tmp32 = 0;")
            L.append("    uint64_t tmp64 = 0;")
            L.append("    size_t blen = 0;")
            L.append("    (void)tmp32; (void)tmp64; (void)blen;")
        for i, f in enumerate(fields):
            t, name = f["type"], f["name"]
            L.append(f"    if (present > {i}) {{")
            if t == "bool":
                L.append(f"        if (!nev_cbor_r_bool(&r, &out->{name})) return NEV_ERR_INVALID_ARG;")
            elif t == "f32":
                L.append(f"        if (!nev_cbor_r_f32(&r, &out->{name})) return NEV_ERR_INVALID_ARG;")
            elif t == "str":
                L.append(
                    f"        if (!nev_cbor_r_text(&r, out->{name}, sizeof(out->{name}))) return NEV_ERR_INVALID_ARG;"
                )
            elif t == "bytes":
                L.append(
                    f"        if (!nev_cbor_r_bytes(&r, &out->{name}, &blen)) return NEV_ERR_INVALID_ARG;"
                )
                L.append(f"        if (blen > {f['max']}u) return NEV_ERR_NO_SPACE;")
                L.append(f"        out->{name}_len = blen;")
            elif t == "i32":
                L.append(f"        if (!nev_cbor_r_i32(&r, &out->{name})) return NEV_ERR_INVALID_ARG;")
            elif t == "u64":
                L.append(f"        if (!nev_cbor_r_u64(&r, &out->{name})) return NEV_ERR_INVALID_ARG;")
            else:
                L.append("        if (!nev_cbor_r_u32(&r, &tmp32)) return NEV_ERR_INVALID_ARG;")
                limit = C_SCALAR[t][2]
                if limit:
                    L.append(f"        if (tmp32 > {limit}) return NEV_ERR_INVALID_ARG;")
                L.append(f"        out->{name} = ({c_type(f)})tmp32;")
            L.append("    }")
        L += [
            "    /* Fields appended by a newer peer: stepped over, not an error. */",
            f"    for (size_t i = {len(fields)}; i < present; i++) {{",
            "        if (!nev_cbor_r_skip(&r)) return NEV_ERR_INVALID_ARG;",
            "    }",
            "    return NEV_OK;",
            "}",
            "",
        ]

    L += [
        "nev_err_t nev_proto_peek_id(const uint8_t *buf, size_t len, uint16_t *out_id) {",
        "    if (!buf || !out_id) return NEV_ERR_INVALID_ARG;",
        "    nev_cbor_r_t r;",
        "    nev_cbor_r_init(&r, buf, len);",
        "    size_t count = 0;",
        "    if (!nev_cbor_r_array(&r, &count) || count < 1) return NEV_ERR_INVALID_ARG;",
        "    uint32_t id = 0;",
        "    if (!nev_cbor_r_u32(&r, &id) || id > 0xFFFFu) return NEV_ERR_INVALID_ARG;",
        "    *out_id = (uint16_t)id;",
        "    return NEV_OK;",
        "}",
        "",
        "nev_err_t nev_proto_frame_wrap(const uint8_t *payload, size_t payload_len, uint8_t *buf,",
        "                               size_t cap, size_t *out_len) {",
        "    if (!payload || !buf) return NEV_ERR_INVALID_ARG;",
        "    if (payload_len > NEV_PROTO_MAX_FRAME) return NEV_ERR_NO_SPACE;",
        "    if (cap < payload_len + NEV_PROTO_FRAME_HEADER) return NEV_ERR_NO_SPACE;",
        "    buf[0] = (uint8_t)(payload_len >> 24);",
        "    buf[1] = (uint8_t)(payload_len >> 16);",
        "    buf[2] = (uint8_t)(payload_len >> 8);",
        "    buf[3] = (uint8_t)payload_len;",
        "    memcpy(buf + NEV_PROTO_FRAME_HEADER, payload, payload_len);",
        "    if (out_len) *out_len = payload_len + NEV_PROTO_FRAME_HEADER;",
        "    return NEV_OK;",
        "}",
        "",
        "nev_err_t nev_proto_frame_split(const uint8_t *buf, size_t len, const uint8_t **payload,",
        "                                size_t *payload_len, size_t *frame_len) {",
        "    if (!buf) return NEV_ERR_INVALID_ARG;",
        "    if (len < NEV_PROTO_FRAME_HEADER) return NEV_ERR_TIMEOUT; /* need more */",
        "    const uint32_t declared = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |",
        "                              ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];",
        "    /* Checked before it is trusted: a length prefix is the first thing an",
        "     * attacker controls, and the device cannot allocate its way out of a lie. */",
        "    if (declared > NEV_PROTO_MAX_FRAME) return NEV_ERR_NO_SPACE;",
        "    if (len - NEV_PROTO_FRAME_HEADER < declared) return NEV_ERR_TIMEOUT;",
        "    if (payload) *payload = buf + NEV_PROTO_FRAME_HEADER;",
        "    if (payload_len) *payload_len = declared;",
        "    if (frame_len) *frame_len = declared + NEV_PROTO_FRAME_HEADER;",
        "    return NEV_OK;",
        "}",
        "",
    ]
    return "\n".join(L)


def c_literal(f, v):
    t = f["type"]
    if t == "bool":
        return "true" if v else "false"
    if t == "f32":
        return f"{v}f"
    if t == "str":
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if t == "bytes":
        return "{" + ", ".join(f"0x{b:02X}" for b in v) + "}"
    return f"{v}u" if t.startswith("u") else str(v)


def gen_c_golden(spec):
    L = [
        "/*",
        f" * {BANNER}",
        " *",
        " * Canonical encodings produced by the Python reference implementation in",
        " * tools/schema/gen.py. The C and Rust codecs are both checked against these, so",
        " * a disagreement between any two of the three is caught rather than shipped.",
        " */",
        "#ifndef NEV_BRIDGE_GOLDEN_DATA_H",
        "#define NEV_BRIDGE_GOLDEN_DATA_H",
        "",
        '#include "nev_bridge/nev_proto.h"',
        "#include <stdio.h>",
        "#include <string.h>",
        "",
    ]
    for m in spec["message"]:
        blob = encode_sample(m)
        L.append(f"static const uint8_t kGolden_{m['name']}[] = {{")
        L.append("    " + ", ".join(f"0x{b:02X}" for b in blob))
        L.append("};")
        for i, f in enumerate(m.get("field", [])):
            v = sample_value(f, i)
            if f["type"] == "bytes":
                L.append(
                    f"static const uint8_t kGolden_{m['name']}_{f['name']}[] = {c_literal(f, v)};"
                )
        # The expected struct, so a test compares decode output directly rather
        # than restating the schema by hand.
        L.append(f"static nev_msg_{m['name']}_t golden_sample_{m['name']}(void) {{")
        L.append(f"    nev_msg_{m['name']}_t s;")
        L.append("    memset(&s, 0, sizeof(s));")
        for i, f in enumerate(m.get("field", [])):
            v = sample_value(f, i)
            if f["type"] == "bytes":
                L.append(f"    s.{f['name']} = kGolden_{m['name']}_{f['name']};")
                L.append(f"    s.{f['name']}_len = sizeof(kGolden_{m['name']}_{f['name']});")
            elif f["type"] == "str":
                L.append(f"    snprintf(s.{f['name']}, sizeof(s.{f['name']}), \"%s\", {c_literal(f, v)});")
            else:
                L.append(f"    s.{f['name']} = {c_literal(f, v)};")
        if not m.get("field"):
            L.append("    (void)s;")
        L.append("    return s;")
        L.append("}")
        L.append("")
    L += ["#endif /* NEV_BRIDGE_GOLDEN_DATA_H */", ""]
    return "\n".join(L)


# --------------------------------------------------------------------- Rust


def rust_type(f):
    t = f["type"]
    if t == "str":
        return "String"
    if t == "bytes":
        return "Vec<u8>"
    return RUST_SCALAR[t]


def gen_rust(spec):
    L = [
        f"// {BANNER}",
        "#![allow(clippy::all)]",
        "",
        "use crate::cbor::{CborReader, CborWriter, ProtoError};",
        "",
        f"pub const PROTOCOL_VERSION: u16 = {spec['protocol_version']};",
        f"pub const MAX_FRAME_BYTES: usize = {spec['max_frame_bytes']};",
        "pub const FRAME_HEADER: usize = 4;",
        "",
        "#[derive(Debug, Clone, Copy, PartialEq, Eq)]",
        "pub enum MsgId {",
    ]
    for m in spec["message"]:
        L.append(f"    {pascal(m['name'])} = {m['id']},")
    L += [
        "}",
        "",
        "impl MsgId {",
        "    pub fn from_u16(v: u16) -> Option<Self> {",
        "        match v {",
    ]
    for m in spec["message"]:
        L.append(f"            {m['id']} => Some(MsgId::{pascal(m['name'])}),")
    L += ["            _ => None,", "        }", "    }", "", "    pub fn name(self) -> &'static str {", "        match self {"]
    for m in spec["message"]:
        L.append(f"            MsgId::{pascal(m['name'])} => \"{m['name']}\",")
    L += ["        }", "    }", "}", ""]

    for m in spec["message"]:
        doc = (m.get("doc") or "").strip()
        for line in doc.splitlines():
            if line.strip():
                L.append(f"/// {line.strip()}")
        L.append("#[derive(Debug, Clone, Default, PartialEq)]")
        L.append(f"pub struct {pascal(m['name'])} {{")
        for f in m.get("field", []):
            fdoc = (f.get("doc") or "").strip()
            if fdoc:
                L.append(f"    /// {fdoc}")
            L.append(f"    pub {rust_ident(f['name'])}: {rust_type(f)},")
        L.append("}")
        L.append("")
        L.append(f"impl {pascal(m['name'])} {{")
        L.append(f"    pub const ID: u16 = {m['id']};")
        L.append("")
        L.append("    pub fn encode(&self) -> Vec<u8> {")
        L.append("        let mut w = CborWriter::new();")
        L.append(f"        w.array({len(m.get('field', [])) + 1});")
        L.append(f"        w.u64({m['id']});")
        for f in m.get("field", []):
            name = rust_ident(f["name"])
            t = f["type"]
            if t == "bool":
                L.append(f"        w.bool(self.{name});")
            elif t == "f32":
                L.append(f"        w.f32(self.{name});")
            elif t == "str":
                L.append(f"        w.text(&self.{name});")
            elif t == "bytes":
                L.append(f"        w.bytes(&self.{name});")
            elif t == "i32":
                L.append(f"        w.i64(self.{name} as i64);")
            else:
                L.append(f"        w.u64(self.{name} as u64);")
        L.append("        w.finish()")
        L.append("    }")
        L.append("")
        L.append("    pub fn decode(buf: &[u8]) -> Result<Self, ProtoError> {")
        L.append("        let mut r = CborReader::new(buf);")
        L.append("        let count = r.array()?;")
        L.append("        if count < 1 { return Err(ProtoError::Malformed); }")
        L.append("        let id = r.u64()?;")
        L.append(f"        if id != {m['id']} {{ return Err(ProtoError::WrongMessage); }}")
        L.append("        let present = count - 1;")
        L.append("        let mut out = Self::default();")
        for i, f in enumerate(m.get("field", [])):
            name = rust_ident(f["name"])
            t = f["type"]
            L.append(f"        if present > {i} {{")
            if t == "bool":
                L.append(f"            out.{name} = r.bool()?;")
            elif t == "f32":
                L.append(f"            out.{name} = r.f32()?;")
            elif t == "str":
                L.append(f"            out.{name} = r.text({f['max']})?;")
            elif t == "bytes":
                L.append(f"            out.{name} = r.bytes({f['max']})?;")
            elif t == "i32":
                L.append(f"            out.{name} = r.i64()? as i32;")
            else:
                mx = {"u8": "u8::MAX as u64", "u16": "u16::MAX as u64", "u32": "u32::MAX as u64"}.get(t)
                L.append("            let v = r.u64()?;")
                if mx:
                    L.append(f"            if v > {mx} {{ return Err(ProtoError::OutOfRange); }}")
                L.append(f"            out.{name} = v as {RUST_SCALAR[t]};")
            L.append("        }")
        L.append(f"        // Fields appended by a newer peer are stepped over, not an error.")
        L.append(f"        for _ in {len(m.get('field', []))}..present {{ r.skip()?; }}")
        L.append("        Ok(out)")
        L.append("    }")
        L.append("}")
        L.append("")

    L += [
        "/// Reads the message id without decoding the body, so a receiver can dispatch.",
        "pub fn peek_id(buf: &[u8]) -> Result<u16, ProtoError> {",
        "    let mut r = CborReader::new(buf);",
        "    let count = r.array()?;",
        "    if count < 1 { return Err(ProtoError::Malformed); }",
        "    let id = r.u64()?;",
        "    if id > u16::MAX as u64 { return Err(ProtoError::OutOfRange); }",
        "    Ok(id as u16)",
        "}",
        "",
        "/// Wraps a payload in its big-endian length prefix.",
        "pub fn frame_wrap(payload: &[u8]) -> Result<Vec<u8>, ProtoError> {",
        "    if payload.len() > MAX_FRAME_BYTES { return Err(ProtoError::TooLong); }",
        "    let mut out = Vec::with_capacity(payload.len() + FRAME_HEADER);",
        "    out.extend_from_slice(&(payload.len() as u32).to_be_bytes());",
        "    out.extend_from_slice(payload);",
        "    Ok(out)",
        "}",
        "",
        "/// Returns the payload range of the first complete frame, and its total length.",
        "/// `Ok(None)` means more bytes are needed.",
        "pub fn frame_split(buf: &[u8]) -> Result<Option<(std::ops::Range<usize>, usize)>, ProtoError> {",
        "    if buf.len() < FRAME_HEADER { return Ok(None); }",
        "    let declared = u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;",
        "    // Checked before it is trusted: a length prefix is the first thing a peer controls.",
        "    if declared > MAX_FRAME_BYTES { return Err(ProtoError::TooLong); }",
        "    if buf.len() - FRAME_HEADER < declared { return Ok(None); }",
        "    Ok(Some((FRAME_HEADER..FRAME_HEADER + declared, FRAME_HEADER + declared)))",
        "}",
        "",
    ]
    return "\n".join(L)


def rust_literal(f, v):
    t = f["type"]
    if t == "bool":
        return "true" if v else "false"
    if t == "f32":
        return f"{v}f32"
    if t == "str":
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '".to_string()'
    if t == "bytes":
        return "vec![" + ", ".join(f"0x{b:02X}" for b in v) + "]"
    return str(v)


def gen_rust_golden(spec):
    L = [
        f"// {BANNER}",
        "//",
        "// The same canonical vectors the C tests use. If the two codecs ever disagree,",
        "// one of these suites fails rather than a field arriving corrupt in production.",
        "#![allow(clippy::all)]",
        "",
        "use crate::generated::*;",
        "",
    ]
    for m in spec["message"]:
        blob = encode_sample(m)
        L.append(f"pub const GOLDEN_{m['name'].upper()}: &[u8] = &[")
        L.append("    " + ", ".join(f"0x{b:02X}" for b in blob))
        L.append("];")
        L.append("")
        L.append(f"pub fn sample_{m['name']}() -> {pascal(m['name'])} {{")
        L.append(f"    {pascal(m['name'])} {{")
        for i, f in enumerate(m.get("field", [])):
            L.append(f"        {rust_ident(f['name'])}: {rust_literal(f, sample_value(f, i))},")
        L.append("    }")
        L.append("}")
        L.append("")
    return "\n".join(L)


# ------------------------------------------------------------------- helpers

RUST_KEYWORDS = {"final", "type", "match", "move", "ref", "box", "fn", "loop", "mod"}


def pascal(s):
    return "".join(p.capitalize() for p in s.split("_"))


def rust_ident(s):
    return f"r#{s}" if s in RUST_KEYWORDS else s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="fail if the checked-in output is stale")
    args = ap.parse_args()

    spec = tomllib.loads(SCHEMA.read_text())

    ids = {}
    for m in spec["message"]:
        if m["id"] in ids:
            raise SystemExit(f"duplicate message id {m['id']}: {m['name']} and {ids[m['id']]}")
        ids[m["id"]] = m["name"]
        for f in m.get("field", []):
            if f["type"] in ("str", "bytes") and "max" not in f:
                raise SystemExit(f"{m['name']}.{f['name']}: {f['type']} needs a max")

    outputs = {
        ROOT / "components/nev_bridge/include/nev_bridge/nev_proto.h": gen_c_header(spec),
        ROOT / "components/nev_bridge/src/nev_proto.c": gen_c_source(spec),
        ROOT / "components/nev_bridge/test/golden_data.h": gen_c_golden(spec),
        ROOT / "companion/nevos-proto/src/generated.rs": gen_rust(spec),
        ROOT / "companion/nevos-proto/src/golden.rs": gen_rust_golden(spec),
    }
    for m in spec["message"]:
        outputs[ROOT / "schema/golden" / f"{m['name']}.cbor"] = encode_sample(m)

    stale = []
    for path, content in outputs.items():
        data = content if isinstance(content, bytes) else content.encode()
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                stale.append(path.relative_to(ROOT))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    if args.check:
        if stale:
            print("generated files are stale — run ./tools/build.sh proto", file=sys.stderr)
            for p in stale:
                print(f"  {p}", file=sys.stderr)
            return 1
        print(f"protocol: {len(spec['message'])} messages, generated output up to date")
        return 0

    print(f"protocol v{spec['protocol_version']}: {len(spec['message'])} messages")
    for path in outputs:
        print(f"  {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
