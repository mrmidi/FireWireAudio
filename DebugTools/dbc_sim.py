import logging
import math
import os

# Simulation parameters
NUM_PACKETS = 8000
TICKS_PER_CYCLE = 3072
BASE_INC_441 = 565
PHASE_MOD = 146
SYT_INTERVAL = 8

# Configure logging
os.makedirs('logs', exist_ok=True)
logging.basicConfig(filename='logs/dbc_simulation.log',
                    level=logging.DEBUG,
                    format='%(asctime)s %(levelname)s %(message)s')

def simulate_packets(num_packets):
    calc_state = {
        'sytOffset': TICKS_PER_CYCLE,
        'sytPhase': 0,
        'dbc': 0,
        'prevWasNoData': True
    }
    errors = []

    last_data_dbc = None
    last_pkt_dbc = None
    prev_no_data = calc_state['prevWasNoData']

    for pkt_idx in range(num_packets):
        # Determine no-data vs data packet
        no_data = calc_state['sytOffset'] >= TICKS_PER_CYCLE
        if calc_state['sytOffset'] < TICKS_PER_CYCLE:
            inc = BASE_INC_441
            idx = calc_state['sytPhase'] % PHASE_MOD
            if (idx and not (idx & 3)) or (calc_state['sytPhase'] == PHASE_MOD - 1):
                inc += 1
            calc_state['sytOffset'] += inc
        else:
            calc_state['sytOffset'] -= TICKS_PER_CYCLE
        calc_state['sytPhase'] = (calc_state['sytPhase'] + 1) % PHASE_MOD

        # Compute DBC
        if no_data:
            expected = ((last_data_dbc + SYT_INTERVAL) % 256) if last_data_dbc is not None else None
            calc_state['dbc'] = (calc_state['dbc'] + SYT_INTERVAL) & 0xFF
            calc_state['prevWasNoData'] = True
        else:
            if calc_state['prevWasNoData']:
                # first data after no-data: no increment
                expected = last_pkt_dbc
                calc_state['prevWasNoData'] = False
            else:
                expected = ((last_data_dbc + SYT_INTERVAL) % 256) if last_data_dbc is not None else None
                calc_state['dbc'] = (calc_state['dbc'] + SYT_INTERVAL) & 0xFF

        current_dbc = calc_state['dbc']
        # Continuity check
        if expected is not None and current_dbc != expected:
            errors.append((pkt_idx, current_dbc, expected, no_data, prev_no_data, last_data_dbc, last_pkt_dbc))
            logging.critical(f"Continuity ERROR @pkt {pkt_idx}: got 0x{current_dbc:02X}, expected 0x{expected:02X}"
                             f" (no_data={no_data}, prev_no_data={prev_no_data}, "
                             f"last_data=0x{last_data_dbc:02X} if last_data_dbc else None, "
                             f"last_pkt=0x{last_pkt_dbc:02X} if last_pkt_dbc else None)")

        # Update history
        if not no_data:
            last_data_dbc = current_dbc
        last_pkt_dbc = current_dbc
        prev_no_data = no_data

        # Log every packet
        logging.debug(f"pkt {pkt_idx}: no_data={no_data}, dbc=0x{current_dbc:02X}, "
                      f"prevWasNoData={calc_state['prevWasNoData']}")

    # Summary
    with open('logs/summary.log', 'w') as f:
        f.write(f"Total packets: {num_packets}\n")
        f.write(f"Total errors: {len(errors)}\n")
        f.write(f"No-data packets: {sum(1 for e in errors if e[3])}\n")
        f.write(f"Data packets: {sum(1 for e in errors if not e[3])}\n")
    return errors

if __name__ == "__main__":
    simulate_packets(NUM_PACKETS)
