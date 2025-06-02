# phase_vs_linux_fixed.py

import sys

TICKS_PER_CYCLE = 3072           # 3072 ticks = 1 FireWire cycle (125 µs)
SYT_PHASE_MOD    = 147           # Apple’s 44.1 kHz phase loop length
BASE_INCREMENT   = 1386          # ⌊(⟂24576000/44100)⌋ = 1386 ticks “base” per packet

def apple_sequence(n_packets: int):
    """
    Yield True/False for each of the first n_packets according to Apple’s
    original 44.1 kHz “blocking” phase‐accumulator algorithm (8 frames/packet, 8 000 cycles/sec).
      True  → “DATA” packet  (syt_offset < 3072)
      False → “NO-DATA” packet (syt_offset >= 3072, returns CIP_SYT_NO_INFO)
    Implements exactly the same two-step logic as Linux’s calculate_syt_offset():
      1) If last_raw < 3072, then raw = last_raw + inc; else raw = last_raw − 3072.
      2) last_raw = raw; if raw ≥ 3072 → NO-DATA; else → DATA.
    """
    phase = 0
    last_raw = 0  # corresponds to “*last_syt_offset” in the C code

    for _ in range(n_packets):
        # Step A: compute “raw” next syt_offset (in ticks) following Apple’s C code
        if last_raw < TICKS_PER_CYCLE:
            # compute increment = 1386 or 1387 when (phase % 13 in {4,8,12}) or (phase == 146)
            inc = BASE_INCREMENT
            idx = phase % 13
            if (idx != 0 and (idx & 3) == 0) or (phase == SYT_PHASE_MOD - 1):
                inc += 1
            raw = last_raw + inc
        else:
            # if last_raw ≥ 3072, subtract 3072 once (no “+inc” in this path)
            raw = last_raw - TICKS_PER_CYCLE

        # Step B: decide DATA vs NO-DATA based on raw
        is_data = (raw < TICKS_PER_CYCLE)

        # “last_raw” is updated to raw exactly as in the C code, before any clipping
        last_raw = raw

        # advance phase in [0..146]
        phase = (phase + 1) % SYT_PHASE_MOD

        yield is_data


def linux_sequence(n_packets: int):
    """
    Yield True/False for each of the first n_packets according to Linux’s
    “ideal_nonblocking” AMDTP code for 44.1 kHz.
      True  → “DATA” packet  (syt_offset < 3072)
      False → “NO-DATA” packet
    (Implements calculate_syt_offset exactly as in the Linux driver excerpt.)
    """
    last_syt_offset  = 0
    syt_offset_state = 0

    for _ in range(n_packets):
        if last_syt_offset < TICKS_PER_CYCLE:
            # same increment logic as Apple’s “phase % 13” +146 special case
            idx = syt_offset_state % 13
            inc = BASE_INCREMENT
            if (idx != 0 and (idx & 3) == 0) or (syt_offset_state == SYT_PHASE_MOD - 1):
                inc += 1
            syt_offset = last_syt_offset + inc
            syt_offset_state = (syt_offset_state + 1) % SYT_PHASE_MOD
        else:
            syt_offset = last_syt_offset - TICKS_PER_CYCLE

        # Now decide DATA vs NO-DATA
        is_data = (syt_offset < TICKS_PER_CYCLE)

        last_syt_offset = syt_offset
        yield is_data


def simulate(algorithm_fn, duration_seconds: int):
    """
    Run one of the two algorithms (apple_sequence or linux_sequence) for
    duration_seconds, counting DATA vs NO-DATA. Returns (data/sec, no_data/sec, total/sec, ratio).
    """
    CYCLES_PER_SEC = 8000
    total_packets = CYCLES_PER_SEC * duration_seconds

    data_count = 0
    no_data_count = 0

    seq = algorithm_fn(total_packets)
    for is_data in seq:
        if is_data:
            data_count += 1
        else:
            no_data_count += 1

    data_per_sec    = data_count / duration_seconds
    no_data_per_sec = no_data_count / duration_seconds
    total_per_sec   = (data_count + no_data_count) / duration_seconds
    ratio           = data_count / no_data_count if no_data_count else float('inf')

    return data_per_sec, no_data_per_sec, total_per_sec, ratio


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Compare Apple vs Linux 44.1 kHz→8 kHz packet algorithms (fixed Apple)."
    )
    parser.add_argument(
        "-n", "--packets", type=int, default=40,
        help="How many of the first N packets to print (default: 40)"
    )
    parser.add_argument(
        "-d", "--duration", type=int, default=60,
        help="Seconds to simulate for the summary (default: 60)"
    )
    args = parser.parse_args()

    N = args.packets
    D = args.duration

    # 1) Print first N packets for both algorithms
    print(f"First {N} packets → APPLE algorithm (fixed):")
    for i, is_data in enumerate(apple_sequence(N), start=1):
        label = "DATA" if is_data else "NO-DATA"
        print(f"{i:2d}:  {label}")

    print(f"\nFirst {N} packets → LINUX ideal_nonblocking algorithm:")
    for i, is_data in enumerate(linux_sequence(N), start=1):
        label = "DATA" if is_data else "NO-DATA"
        print(f"{i:2d}:  {label}")

    # 2) Simulate for D seconds and print summary
    print("\nSummary over", D, "seconds:")
    a_data, a_no, a_tot, a_ratio = simulate(apple_sequence, D)
    print("APPLE algorithm (fixed):")
    print(f"  Data  packets/sec: {a_data:.4f}")
    print(f"  No-Data packets/sec: {a_no:.4f}")
    print(f"  Total packets/sec:   {a_tot:.4f}")
    print(f"  Data/No-Data ratio:  {a_ratio:.6f}")

    print("\nLINUX ideal_nonblocking algorithm:")
    l_data, l_no, l_tot, l_ratio = simulate(linux_sequence, D)
    print(f"  Data  packets/sec: {l_data:.4f}")
    print(f"  No-Data packets/sec: {l_no:.4f}")
    print(f"  Total packets/sec:   {l_tot:.4f}")
    print(f"  Data/No-Data ratio:  {l_ratio:.6f}")