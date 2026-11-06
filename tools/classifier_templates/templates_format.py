"""templates_format.py — read/write the classifier's templates.bin format.

This is the dev-machine counterpart to app/classifier/classifier.c's
templates_load(). Keep the two in sync — the format is:

    char     magic[4]      = b"SCFT"
    uint32_t version       = 1, 2 or 3
    uint32_t count
    uint32_t canonical_n
    then `count` entries of:
        char     label[32]   (UTF-8, NUL-padded/truncated)
        uint32_t template_id
        float32  data[canonical_n]   (little-endian)
        -- version 2 only, appended per entry --
        float32  bandwidth_frac_lo, bandwidth_frac_hi
        float32  papr_db_lo,        papr_db_hi
        float32  flatness_lo,       flatness_hi
        float32  peak_count_lo,     peak_count_hi
        -- version 3 only, appended per entry (after the ranges) --
        float32  hz_per_bin          (0 = no resolution, matched bin-for-bin)

A version-3 template records its resolution: the on-device classifier
resamples it onto the live frame's grid (canonical_n * hz_per_bin /
live_hz_per_bin bins) before matching, so the shape covers the same
hertz at any span. Version 1/2 templates have no resolution and are
matched bin-for-bin, which only works at the span they were made for.

A version-2 range with hi < lo means "unbounded" (no gate on that
feature) — see FeatureRanges.UNBOUNDED below. Version 1 files behave as
if every range were unbounded: the on-device classifier applies no gate
to them, so old templates.bin files keep classifying exactly as before.

Templates must already be normalized (zero-mean, unit L2 norm) before
writing — the on-device loader trusts the file as-is and does not
re-normalize on load, only the *live* frame gets normalized at runtime.
Use normalize() below before passing data to write_templates().

The feature ranges (when present) are the same four scalars
app/classifier/classifier.c computes per live frame — see features.py,
kept in lockstep with the C implementation.
"""
import struct
from dataclasses import dataclass, field
from typing import Optional, Sequence

MAGIC = b"SCFT"
VERSION_1 = 1
VERSION_2 = 2
VERSION_3 = 3
LABEL_LEN = 32

# hi < lo signals "no gate on this feature" to the on-device loader.
_UNBOUNDED = (1.0, -1.0)


@dataclass
class FeatureRanges:
    """Expected (lo, hi) range per feature, for a version-2 template's soft
    match gate (see classifier.c's template_gate_factor()). Any field left
    as UNBOUNDED is not gated. Construct these via from_features() below
    rather than by hand in normal use."""
    bandwidth_frac: tuple = _UNBOUNDED
    papr_db: tuple = _UNBOUNDED
    flatness: tuple = _UNBOUNDED
    peak_count: tuple = _UNBOUNDED

    UNBOUNDED = _UNBOUNDED

    @staticmethod
    def from_samples(bandwidth_fracs, papr_dbs, flatnesses, peak_counts, margin: float = 0.15):
        """Build a FeatureRanges from one or more observed samples of each
        feature (e.g. per-frame values from several captures, or a single
        synthesized template's own computed features), widened by `margin`
        as a fraction of the observed spread (or of the value itself, for a
        single sample) so a range built from a handful of samples isn't
        pathologically tight."""
        def widen(values):
            lo, hi = min(values), max(values)
            pad = (hi - lo) * margin if hi > lo else max(abs(lo), 1e-6) * margin
            return (lo - pad, hi + pad)

        return FeatureRanges(
            bandwidth_frac=widen(bandwidth_fracs),
            papr_db=widen(papr_dbs),
            flatness=widen(flatnesses),
            peak_count=widen(peak_counts),
        )


@dataclass
class Template:
    label: str
    template_id: int
    data: Sequence[float]  # length must equal canonical_n for the file
    feature_ranges: Optional[FeatureRanges] = field(default=None)
    hz_per_bin: float = 0.0  # version 3; 0 = no resolution


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

    # Version 2 only if at least one template actually carries ranges —
    # otherwise stay on version 1 so a plain shape-only library round-trips
    # through unmodified older tooling/devices exactly as it always has.
    version = VERSION_2 if any(t.feature_ranges is not None for t in templates) else VERSION_1
    if any(t.hz_per_bin > 0 for t in templates):
        version = VERSION_3

    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<I", version))
        f.write(struct.pack("<I", len(templates)))
        f.write(struct.pack("<I", canonical_n))
        for t in templates:
            label_bytes = t.label.encode("utf-8")[: LABEL_LEN - 1]
            f.write(label_bytes.ljust(LABEL_LEN, b"\x00"))
            f.write(struct.pack("<I", t.template_id))
            f.write(struct.pack(f"<{canonical_n}f", *t.data))
            if version >= VERSION_2:
                fr = t.feature_ranges or FeatureRanges()
                for lo, hi in (fr.bandwidth_frac, fr.papr_db, fr.flatness, fr.peak_count):
                    f.write(struct.pack("<ff", lo, hi))
            if version >= VERSION_3:
                f.write(struct.pack("<f", float(t.hz_per_bin)))


def read_templates(path: str):
    """Round-trip reader, mainly for tests/inspection — not used on-device
    (that's classifier.c's job), but handy for `python -m
    templates_format inspect templates.bin`-style sanity checks."""
    with open(path, "rb") as f:
        magic = f.read(4)
        if magic != MAGIC:
            raise ValueError(f"bad magic {magic!r}, expected {MAGIC!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version not in (VERSION_1, VERSION_2, VERSION_3):
            raise ValueError(f"unsupported version {version}")
        (count,) = struct.unpack("<I", f.read(4))
        (canonical_n,) = struct.unpack("<I", f.read(4))
        templates = []
        for _ in range(count):
            label_bytes = f.read(LABEL_LEN)
            label = label_bytes.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
            (template_id,) = struct.unpack("<I", f.read(4))
            data = struct.unpack(f"<{canonical_n}f", f.read(4 * canonical_n))
            feature_ranges = None
            hz_per_bin = 0.0
            if version >= VERSION_2:
                ranges = []
                for _ in range(4):
                    lo, hi = struct.unpack("<ff", f.read(8))
                    ranges.append((lo, hi))
                feature_ranges = FeatureRanges(*ranges)
                if all(hi < lo for lo, hi in ranges):
                    feature_ranges = None  # all unbounded: carry it as "no gate"
            if version >= VERSION_3:
                (hz_per_bin,) = struct.unpack("<f", f.read(4))
            templates.append(Template(label=label, template_id=template_id, data=list(data),
                                       feature_ranges=feature_ranges, hz_per_bin=hz_per_bin))
        return canonical_n, templates
