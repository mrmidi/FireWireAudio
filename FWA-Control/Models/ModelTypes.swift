// filepath: /Users/mrmidi/DEV/FWControl/FWControl/Models/ModelTypes.swift
// This file re-exports model types from dummy.swift to make them accessible throughout the app

import Foundation

// We're not using @_exported import because FWControl is not a module.
// Instead, we'll directly access the types by including dummy.swift in the project.

// This file serves as documentation of the model types available
// and their relationships.

/*
Key model types used in the application:
- DeviceInfo: Top-level container for all FireWire device information
- AudioPlugInfo: Represents a single plug (input or output) on a device
- MusicSubunitInfo: Contains information about a Music subunit
- AudioSubunitInfo: Contains information about an Audio subunit
- AVCInfoBlockInfo: Represents an AV/C Info Block structure
- PlugConnectionInfo: Details of plug connections
- AudioStreamFormat: Format information for audio streams

Key enums:
- PlugDirection: Input or Output
- PlugUsage: How the plug is used (isochronous, external, etc.)
- SubunitType: Type of subunit (Music, Audio, etc.)
*/
