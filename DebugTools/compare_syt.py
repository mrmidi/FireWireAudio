#!/usr/bin/env python3
import re, csv, sys

def parse_firebug(path):
    """
    Return a list of (index, channel, size, syt_be, syt) in raw order.
    """
    pat_hdr = re.compile(r'Isoch channel\s+([01]),.*?size\s+(\d+)')
    pat_hex = re.compile(r'\b([0-9A-Fa-f]{8})\b')
    entries = []
    idx = 0
    with open(path, 'r') as f:
        lines = f.readlines()
    i = 0
    while i < len(lines)-1:
        m = pat_hdr.search(lines[i])
        if m:
            ch, size = int(m.group(1)), int(m.group(2))
            words = pat_hex.findall(lines[i+1])
            if len(words) >= 2:
                w1,w2 = words[0], words[1]
                b = bytes.fromhex(w1 + w2)
                syt_be = (b[6]<<8) | b[7]
                # convert big-endian to host-endian
                syt = ((syt_be & 0xFF) << 8) | (syt_be >> 8)
                entries.append({
                    'idx': idx,
                    'channel': ch,
                    'size': size,
                    'syt_be': f"0x{syt_be:04X}",
                    'syt': syt
                })
                idx += 1
            i += 2
        else:
            i += 1
    return entries

def pairwise(entries):
    """
    Yield (ch0_entry, ch1_entry) for each matched pair, in arrival order.
    """
    pending = {0: None, 1: None}
    for e in entries:
        ch = e['channel']
        other = 1-ch
        if pending[other] is not None:
            # We have an unmatched packet from the other channel -> pair it
            yield (pending[other], e) if other==0 else (e, pending[other])
            pending[0] = pending[1] = None
        else:
            # Hold this packet until we see its counterpart
            pending[ch] = e

def write_csv(entries, fname):
    with open(fname, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(entries[0].keys()))
        w.writeheader()
        w.writerows(entries)

def main():
    if len(sys.argv) != 2:
        print("Usage: compare_syt_pairwise.py firebug_dump.txt")
        sys.exit(1)
    path = sys.argv[1]
    entries = parse_firebug(path)
    if not entries:
        print("No packets found.")
        sys.exit(1)

    # Split out by channel
    ch0 = [e for e in entries if e['channel']==0]
    ch1 = [e for e in entries if e['channel']==1]
    print(f"Parsed {len(ch0)} ch0 and {len(ch1)} ch1 packets.")

    write_csv(ch0, "ch0.csv")
    write_csv(ch1, "ch1.csv")
    print("Wrote ch0.csv and ch1.csv")

    # Build pairwise matches
    pairs = list(pairwise(entries))
    print(f"Formed {len(pairs)} pairs for SYT comparison.")

    # Find mismatches
    mismatches = []
    for a,b in pairs:
        if a['syt'] != b['syt']:
            mismatches.append({
                'idx0': a['idx'], 'size0': a['size'], 'syt0_be': a['syt_be'], 'syt0': a['syt'],
                'idx1': b['idx'], 'size1': b['size'], 'syt1_be': b['syt_be'], 'syt1': b['syt'],
            })

    print(f"{len(mismatches)} mismatches out of {len(pairs)} pairs.")
    if mismatches:
        # preview first 10
        print("First mismatches:")
        for mm in mismatches[:10]:
            print(mm)
        # write full report
        with open("mismatches.csv","w",newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(mismatches[0].keys()))
            w.writeheader()
            w.writerows(mismatches)
        print("Wrote mismatches.csv")

if __name__ == "__main__":
    main()