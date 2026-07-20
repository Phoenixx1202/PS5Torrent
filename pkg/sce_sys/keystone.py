#!/usr/bin/env python3
"""
Generate a keystone file for PS5 fPKG.
The keystone is a required file in sce_sys/ for PS5 packages.
For homebrew/fPKG, a minimal fake keystone suffices.

Format (PS5/PS4 compatible):
  Bytes 0-3:    Magic (0x7F1B9C0D for PS4, varies for PS5)
  Bytes 4-7:    Version/Flags
  Bytes 8-39:   Content ID (padded with nulls)
  Bytes 40-79:  Digest/Reserved (zeros for fake)
  Bytes 80-255: Reserved (zeros)
Total: 256 bytes

For PS5 homebrew, the exact format isn't well-documented,
but most tools accept a 256-byte file with Content ID.
"""

import os
import struct

CONTENT_ID = "UP9001-PPSA00001_00-PS5TORRENT000001"


def generate_keystone(content_id=CONTENT_ID):
    """Generate a fake keystone file (256 bytes)."""
    data = bytearray(256)

    # Magic number (PS4-style, accepted by most fPKG tools)
    struct.pack_into('<I', data, 0, 0x7F1B9C0D)

    # Version/flags (0 for fake)
    struct.pack_into('<I', data, 4, 0x00010000)

    # Content ID (null-padded, max 32 bytes)
    cid_bytes = content_id.encode('ascii')
    data[8:8 + min(len(cid_bytes), 32)] = cid_bytes[:32]

    # Rest is all zeros (acceptable for fPKG)
    return bytes(data)


if __name__ == "__main__":
    out_dir = os.path.join(os.path.dirname(__file__))
    keystone_path = os.path.join(out_dir, "keystone")

    print("🔑 Generating keystone file...")
    keystone = generate_keystone()

    with open(keystone_path, "wb") as f:
        f.write(keystone)

    print(f"  ✅ keystone ({len(keystone)} bytes)")
    print(f"     Content ID: {CONTENT_ID}")
    print(f"     Saved: {keystone_path}")
