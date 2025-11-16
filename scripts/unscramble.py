#!/usr/bin/env python3
import sys
from pathlib import Path
from typing import List


def apply_xor_stream(data: bytearray, seed: int) -> None:
    state = seed & 0xFFFFFFFF
    total = len(data)
    if total == 0:
        return
    # Update roughly 1% increments (never slower than every 64 KiB chunk)
    percent_stride = max(1, total // 100)
    stride = min(percent_stride, 64 * 1024)
    for idx in range(total):
        state = (214013 * state + 2531011) & 0xFFFFFFFF
        data[idx] ^= (state >> 24) & 0xFF
        processed = idx + 1
        if processed % stride == 0 or processed == total:
            percent = int(processed * 100 / total)
            sys.stderr.write(f"\r[+] XOR progress: {percent:3d}%")
            sys.stderr.flush()
    sys.stderr.write("\n")


def main(argv: List[str]):
    import argparse

    parser = argparse.ArgumentParser(
        description="Reverse the optional XOR stream applied by vss_client."
    )
    parser.add_argument("input", help="Scrambled ZIP file")
    parser.add_argument("output", nargs="?", help="Output file (defaults to <input>.fixed)")
    parser.add_argument(
        "--xor-seed",
        dest="xor_seed",
        help="Seed used for XOR scrambling (--xor-seed value passed to the creator)",
    )
    args = parser.parse_args(argv)

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Input file not found: {input_path}")
        return 1

    if args.output:
        output_path = Path(args.output)
    else:
        output_path = input_path.with_suffix(input_path.suffix + ".fixed")

    data = bytearray(input_path.read_bytes())
    if args.xor_seed:
        seed = int(args.xor_seed, 0) & 0xFFFFFFFF
        apply_xor_stream(data, seed)
    output_path.write_bytes(data)
    print(f"Patched archive written to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
