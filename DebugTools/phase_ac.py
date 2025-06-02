import pandas as pd

# Configuration for 44.1kHz
sample_rate_hz = 44100
sample_rate_multiplier = sample_rate_hz // 100  # 441
phase_modulo = 80
phase_threshold = 79
base_cycles = sample_rate_multiplier // phase_modulo  # 5
fractional_step = sample_rate_multiplier % phase_modulo  # 41

# Simulation for 20 packets
num_packets = 20
accumulator = 0

records = []

for packet_index in range(num_packets):
    accumulator += fractional_step
    cycles_assigned = base_cycles
    if accumulator > phase_threshold:
        accumulator -= phase_modulo
        cycles_assigned += 1
    
    is_data_packet = (cycles_assigned > base_cycles)
    
    records.append({
        "Packet": packet_index,
        "Accumulator": accumulator,
        "Cycles": cycles_assigned,
        "IsDataPacket": is_data_packet
    })

df = pd.DataFrame(records)

# Count totals
total_data = df["IsDataPacket"].sum()
total_no_data = num_packets - total_data

print(f"\nTotal data packets: {total_data}")
print(f"Total no-data packets: {total_no_data}")

# Display the DataFrame
print("\nDataFrame of packets:")
print(df)
