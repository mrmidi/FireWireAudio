#!/usr/bin/env python3
import re
import argparse
import pandas as pd

def parse_firewire_log(path):
    """
    Parse a FireBug text dump for Isoch packets.
    Returns a pandas DataFrame with columns:
      seq, channel, size, sid, dbs, fn_q, dbc, fmt, fdf, syt_be, syt
    """
    entries = []
    seq = 0
    with open(path, 'r') as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        # Look for the header line
        m = re.search(r'Isoch channel\s+(\d),.*?size\s+(\d+)', lines[i])
        if m and i+1 < len(lines):
            ch   = int(m.group(1))
            size = int(m.group(2))
            # Next line should contain the hex words
            hexline = lines[i+1]
            # Find 8-hex-digit words
            words = re.findall(r'\b([0-9A-Fa-f]{8})\b', hexline)
            if len(words) >= 2:
                w1, w2 = words[0], words[1]
                b = bytes.fromhex(w1 + w2)  # 8 bytes
                sid   = b[0]
                dbs   = b[1]
                fn_q  = b[2]
                dbc   = b[3]
                fmt   = b[4]
                fdf   = b[5]
                syt_be = (b[6] << 8) | b[7]
                # Convert big-endian SYT to host order
                syt     = ((syt_be & 0xFF) << 8) | (syt_be >> 8)
                entries.append({
                    'seq':      seq,
                    'channel':  ch,
                    'size':     size,
                    'sid':      sid,
                    'dbs':      dbs,
                    'fn_q':     fn_q,
                    'dbc':      dbc,
                    'fmt':      fmt,
                    'fdf':      fdf,
                    'syt_be':   f"0x{syt_be:04X}",
                    'syt':      syt,
                })
                seq += 1
            i += 2
        else:
            i += 1

    return pd.DataFrame(entries)


def main():
    p = argparse.ArgumentParser(
        description="Parse FireWire Isoch CIP headers and compare SYT between channels"
    )
    p.add_argument("input",  help="FireBug dump file (text)")
    p.add_argument("prefix", help="Prefix for output CSVs (e.g. 'out' → out_ch0.csv, etc.)")
    args = p.parse_args()

    df = parse_firewire_log(args.input)
    if df.empty:
        print("No Isoch packets found in log.")
        return

    # Split per channel
    for ch in sorted(df['channel'].unique()):
        df[df['channel']==ch].to_csv(f"{args.prefix}_ch{ch}.csv", index=False)
        print(f"Wrote {len(df[df['channel']==ch])} packets → {args.prefix}_ch{ch}.csv")

    # Align by sequence number
    ch0 = df[df['channel']==0].set_index('seq')
    ch1 = df[df['channel']==1].set_index('seq')
    joined = ch0.join(ch1, lsuffix='_0', rsuffix='_1', how='inner')

    # Find SYT mismatches
    mism = joined[joined['syt_0'] != joined['syt_1']]
    print(f"Found {len(mism)} SYT mismatches out of {len(joined)} aligned packets.")
    mism.to_csv(f"{args.prefix}_mismatches.csv")
    print(f"Wrote mismatches → {args.prefix}_mismatches.csv")

if __name__ == "__main__":
    main()