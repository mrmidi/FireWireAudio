```mermaid
graph TD
  %% Core Audio → Driver → Daemon → FireWire HW → Device

  subgraph CoreAudioApp
    A["Application<br/>Writes Audio"]:::app --> B["Core Audio<br/>System Buffer"]:::app
  end

  subgraph FWADriverASPL
    B --> C["FWADriverDevice::DoIOOperation<br/>(ioMainBuffer)"]:::driver
    C --> D["FWADriverHandler::PushToSharedMemory"]:::driver
    D --> E[("RTShmRing SharedMemory<br/>AudioChunk_POD")]:::driver
  end

  subgraph FWADaemon
    E --> F["DaemonCore:<br/>mmap & init SharedRingBuffer_POD"]:::daemon
    F --> G["IsochPacketProvider<br/>bindSharedMemory()"]:::daemon

    subgraph IsochPacketProvider ["IsochPacketProvider"]
      direction TB
      G --> H["pop() zero-copy AudioChunk"]:::daemon
      H --> I["CurrentChunkCache"]:::daemon
      I --> J["fillPacketData() → TxBufMgr"]:::daemon
    end

    J --> K["Isoch::AmdtpTransmitter:<br/>generate CIP & headers"]:::daemon
    K --> L["IsochTransmitDCLManager:<br/>update DCL & notify"]:::daemon
  end

  subgraph FireWireHardware
    L --> M["FireWire Controller:<br/>DMA & Transmit"]:::hardware
  end

  subgraph ExternalDevice
    M --> N["FireWire Audio Device<br/>Receives Packet"]:::external
  end

  %% Styling
  classDef app fill:#f9f,stroke:#333,stroke-width:2px;
  classDef driver fill:#ccf,stroke:#333,stroke-width:2px;
  classDef daemon fill:#cfc,stroke:#333,stroke-width:2px;
  classDef hardware fill:#fcc,stroke:#333,stroke-width:2px;
  classDef external fill:#ffc,stroke:#333,stroke-width:2px;
  ```