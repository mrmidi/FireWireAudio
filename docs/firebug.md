# FireBug Documentation

## Overview

FireBug is a FireWire bus analyzer for TI PCI-Lynx hardware. Commands are case-sensitive. Use lowercase unless specified otherwise.

> Press Command-Q to exit.

## Command Reference

### Filters

#### tcode
- **Description**: Filters packets by tCode values
- **Usage Examples**:
```
tcode on                # Enable all tCodes  
tcode off 4-6          # Exclude tCodes 4, 5, 6  
tcode 1 9 b            # Show tCodes 1, 9, and B (hex)
```

#### ack
- **Description**: Filters packets by acknowledgment type
- **Usage Examples**:
```
ack on                 # Show all ACKs  
ack off 4              # Exclude ACK type 4 (busyX)
```

#### xwindow
- **Description**: Displays packets before/after a filtered packet
- **Usage Examples**:
```
xwindow 3 10          # Show 3 before, 10 after  
xwindow 0             # Disable windowing
```

#### cyclestart
- **Description**: Shows cycle start packets after a trigger
- **Usage Example**:
```
cyclestart 2          # Show 2 cycle starts after trigger
```

#### xisogap
- **Description**: Detects missing isochronous packets on a channel
- **Usage Example**:
```
xisogap 62            # Trigger if channel 62 packets are missing
```

#### isosnap
- **Description**: Captures a burst of isochronous packets
- **Usage Example**:
```
isosnap 20            # Display the next 20 isoch packets
```

#### Node Filtering
- **Commands**: `node`, `src`, `dest`
- **Description**: Filters by source/destination node IDs
- **Usage Examples**:
```
src 1-3 5             # Show packets from nodes 1,2,3,5
dest off 0x1          # Exclude packets sent to node 1
node 3                # Show packets involving node 3
```

#### channel
- **Description**: Filters isochronous packets by channel
- **Usage Example**:
```
channel 3-7 63        # Show channels 3–7 and 63
```

### Display Options

#### timestamp
- **Description**: Toggles timestamp display
- **Usage Example**:
```
timestamp off         # Hide timestamps
```

#### data
- **Description**: Sets the number of payload quadlets to display
- **Usage Example**:
```
data 16              # Show up to 16 quadlets per packet
```

#### windowhidecs
- **Description**: Hides cycle starts in xwindow output
- **Usage Example**:
```
windowhidecs on      # Enable hiding
```

#### phy
- **Description**: Filters PHY packets
- **Usage Example**:
```
phy off             # Hide PHY packets
```

### Utility Commands

#### Basic Controls
- **busreset** (Command-R): Initiates a FireWire bus reset
- **sync** (Command-S): Syncs the local cycle timer with bus cycle starts
- **zero** (Command-Z): Resets all cumulative counters
- **clear** (Command-K): Clears the packet history window

#### Read Commands
- **bread / qread**: Sends block/quadlet read requests
```
bread ffc0fffff000040c 8   # Block read at address 40c (8 bytes)
qread ffc1fffff000040c     # Quadlet read at address 40c
```

#### PHY Operations
- **phyread / phywrite**: Reads/writes PHY registers
```
phyread 0            # Read PHY register 0
phywrite 1 40        # Write 0x40 to PHY register 1 (force reset)
```

#### Configuration Commands
- **phyconfig**: Send PHY configuration packets
```
phyconfig 003c0000   # Send 1394a Global Resume
gap 3f               # Set gap count to 0x3F
root 4               # Force node 4 as root
linkon 1c            # Send link-on to node 0x1C
```

#### Hardware Control
- **gpio**: Controls GPIO pins on Lynx hardware
```
gpio 0 on           # Set GPIO 0 to output high
```

#### Logging
- **log / logto**: Saves packet history to a file
```
log 1000            # Save last 1000 lines
logto ~/firebug.log # Set log filename
```

### Diagnostics

#### Isochronous Monitoring
- **isostats**: Reports isochronous channel statistics
```
isostats off        # Disable stats
```

- **isodup**: Checks for duplicate isoch channels per cycle
```
isodup off          # Disable check
```

- **isowait**: Sets idle cycles before marking a channel inactive
```
isowait 200         # Wait 200 cycles
```

#### CRC Operations
- **lynxCRC / romCRC**: Computes CRC values for Lynx EEPROM/ROM
```
lynxCRC 04 03 02 4c 10 03 80  # Compute CRC for 7 bytes
```

### Additional Settings

#### Display Settings
- **color**: Enables/disables color output
```
color on            # Enable color
```

#### Sampling Controls
- **sampletime / sampleints**: Toggles timestamp/interrupt sampling
```
sampletime off      # Disable timestamp sampling
```

## Important Notes

- Use Command-F to toggle the master filter
- Unmount FireWire disks before running FireBug
- Disable AppleLynx.kext for optimal performance (see readme)