#!/usr/bin/env python3
"""
Down-sample the 512x512 IMG array in Core/Src/image_data.c to 128x128.

Usage:
    python scripts/downsample_image.py

The script reads Core/Src/image_data.c, averages each 4x4 block and rewrites
the file with the smaller array.
"""
from __future__ import annotations

import pathlib
import re
from textwrap import wrap


ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC_PATH = ROOT / "Core" / "Src" / "image_data.c"
FACTOR = 4  # 512 -> 128


def parse_pixels(text: str) -> list[int]:
  start = text.index("{")
  end = text.rindex("}")
  nums = re.findall(r"\b\d+\b", text[start:end])
  return [int(n) for n in nums]


def downsample(data: list[int], width: int, height: int, factor: int) -> list[int]:
  small = []
  for y in range(0, height, factor):
    for x in range(0, width, factor):
      block_sum = 0
      for dy in range(factor):
        row_start = (y + dy) * width + x
        block_sum += sum(data[row_start:row_start + factor])
      small.append(block_sum // (factor * factor))
  return small


def format_array(values: list[int], per_line: int = 16) -> str:
  lines = []
  for i in range(0, len(values), per_line):
    slice_vals = ", ".join(str(v) for v in values[i:i + per_line])
    lines.append(f"  {slice_vals},")
  if lines:
    lines[-1] = lines[-1].rstrip(",")
  return "\n".join(lines)


def main():
  text = SRC_PATH.read_text()
  pixels = parse_pixels(text)
  expected = 512 * 512
  if len(pixels) != expected:
    raise RuntimeError(f"Expected {expected} pixels, found {len(pixels)}.")

  scaled = downsample(pixels, 512, 512, FACTOR)
  if len(scaled) != 128 * 128:
    raise RuntimeError("Unexpected output size.")

  body = format_array(scaled)
  new_text = '#include "image_data.h"\n\n' \
             "const unsigned char IMG[IMG_W*IMG_H] = {\n" \
             f"{body}\n" \
             "};\n"
  SRC_PATH.write_text(new_text)
  print("Wrote down-sampled data back to", SRC_PATH)


if __name__ == "__main__":
  main()

