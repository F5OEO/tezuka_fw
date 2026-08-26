"""templates_format.py — read/write the classifier's templates.bin format.

This is the dev-machine counterpart to app/classifier/classifier.c's
templates_load(). Keep the two in sync — the format is:

    char     magic[4]      = b"SCFT"
    uint32_t version       = 1
    uint32_t count
    uint32_t canonical_n
    then `count` entries of:
        char     label[32]   (UTF-8, NUL-padded/truncated)
        uint32_t template_id
        float32  data[canonical_n]   (little-endian)

Templates must already be normalized (zero-mean, unit L2 norm) before
writing — the on-device loader trusts the file as-is and does not
re-normalize on load, only the *live* frame gets normalized at runtime.
Use normalize() below before passing data to write_templates().
"""
import struct
from dataclasses import dataclass
from typing import Sequence

MAGIC = b"SCFT"
VERSION = 1
LABEL_LEN = 32


@dataclass
class Template:
    label: str
    template_id: int
    data: Sequence[float]  # length must equal canonical_n for the file


def normalize(data: Sequence[float]) -> list:
    """Zero-mean, unit-L2-norm — matches classifier.c's normalize() exactly.
    Raises ValueError on a (near-)zero-energy input, same as the C code
    treating that as "nothing to classify" rather than silently producing
    a divide-by-zero or NaN template."""
    n = len(data)
    if n == 0:
        raise ValueError("empty template data")
    mean = sum(data) / n
    centered = [v - mean for v in data]
    energy = sum(v * v for v in centered)
    if energy < 1e-12:
        raise ValueError("template has ~zero energy after mean removal")
    inv_norm = energy ** -0.5
    return [v * inv_norm for v in centered]


def write_templates(path: str, canonical_n: int, templates: Sequence[Template]) -> None:
    if not templates:
        raise ValueError("at least one template is required")
    for t in templates:
        if len(t.data) != canonical_n:
            raise ValueError(
                f"template {t.label!r} has {len(t.data)} samples, expected canonical_n={canonical_n}"
            )
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<I", VERSION))
        f.write(struct.pack("<I", len(templates)))
        f.write(struct.pack("<I", canonical_n))
        for t in templates:
            label_bytes = t.label.encode("utf-8")[: LABEL_LEN - 1]
            f.write(label_bytes.ljust(LABEL_LEN, b"\x00"))
            f.write(struct.pack("<I", t.template_id))
            f.write(struct.pack(f"<{canonical_n}f", *t.data))


def read_templates(path: str):
    """Round-trip reader, mainly for tests/inspection — not used on-device
    (that's classifier.c's job), but handy for `python -m
    templates_format inspect templates.bin`-style sanity checks."""
    with open(path, "rb") as f:
        magic = f.read(4)
        if magic != MAGIC:
            raise ValueError(f"bad magic {magic!r}, expected {MAGIC!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version != VERSION:
            raise ValueError(f"unsupported version {version}")
        (count,) = struct.unpack("<I", f.read(4))
        (canonical_n,) = struct.unpack("<I", f.read(4))
        templates = []
        for _ in range(count):
            label_bytes = f.read(LABEL_LEN)
            label = label_bytes.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
            (template_id,) = struct.unpack("<I", f.read(4))
            data = struct.unpack(f"<{canonical_n}f", f.read(4 * canonical_n))
            templates.append(Template(label=label, template_id=template_id, data=list(data)))
        return canonical_n, templates
