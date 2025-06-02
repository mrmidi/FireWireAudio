def simulate_44100_over_8000(duration_seconds: int = 60):
    """
    Simulate 44.1 kHz audio (8 frames per packet) over an 8 kHz FireWire cycle clock
    for `duration_seconds` seconds.  Uses a Bresenham‐style accumulator to decide
    which cycles carry real audio (“data”) versus silence (“no‐data”).
    """
    # Number of FireWire cycles per second:
    CYCLES_PER_SEC = 8_000

    # Desired packets/sec = 44 100 frames/sec ÷ 8 frames/packet = 5 512.5 packets/sec
    # We want on average 5 512.5 “data‐packets” out of every 8 000 cycles.
    # Multiply both numerator/denominator by 1000 to keep integers:
    #   data_per_cycle_frac = 5 512.5 / 8 000 = 5512.5 / 8 000
    # We’ll implement: accumulator += 55125; if accumulator >= 80000 → “data”, subtract 80000.
    DATA_INCREMENT   = 55_125   # = 5 512.5 × 10 000  (we scale by 10 000 to avoid floats)
    CYCLE_THRESHOLD  = 80_000   # = 8 000 × 10 (we scaled by the same factor)

    total_cycles = CYCLES_PER_SEC * duration_seconds

    accumulator = 0
    data_count   = 0
    no_data_count = 0

    for _ in range(total_cycles):
        accumulator += DATA_INCREMENT
        if accumulator >= CYCLE_THRESHOLD:
            # send a real‐data packet this cycle
            data_count += 1
            accumulator -= CYCLE_THRESHOLD
        else:
            # send a no‐data packet (silence) this cycle
            no_data_count += 1

    ratio = data_count / no_data_count if no_data_count else float('inf')
    return data_count, no_data_count, ratio

if __name__ == "__main__":
    data, no_data, ratio = simulate_44100_over_8000(60)
    print(f"Total data packets:     {data / 60}")
    print(f"Total no‐data packets:  {no_data / 60}")
    print(f"Total packets per second: {(data + no_data) / 60}")
    print(f"Data / No‐Data ratio:   {ratio:.6f}")