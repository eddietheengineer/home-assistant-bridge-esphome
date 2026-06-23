# Architecture

High-level architecture of the GE Appliances Bridge component.

```mermaid
graph TB
    subgraph ESPHome["ESPHome Framework"]
        YAML["YAML Config"]
        MQTT["MQTT Client"]
        UART1["UART (GEA3)"]
        UART2["UART (GEA2)"]
    end

    subgraph Bridge["GeappliancesBridge"]
        HSM["Startup HSM"]
        DIR["Autodiscovery<br/>Manager"]
        DID["Device Identity<br/>Manager"]
        FBM["Feature Bit<br/>Manager"]
        CACHE["ERD Cache"]
    end

    subgraph Protocol["Protocol Stack"]
        GEA3["GEA3 Interface<br/>& ERD Client"]
        GEA2["GEA2 Interface<br/>& ERD Client"]
        ADAPTER["GEA2→GEA3<br/>Adapter"]
    end

    subgraph Bridges["Data Bridges"]
        POLL["Polling<br/>Bridge"]
        SUB["Subscription<br/>Bridge"]
        WRITE["Write<br/>Bridge"]
    end

    subgraph Publish["MQTT Publishing"]
        PUB["ERD Cache<br/>MQTT Publisher"]
        ADAPT["ESPHome MQTT<br/>Client Adapter"]
    end

    subgraph External["External"]
        APPLIANCE["GE Appliance"]
        HA["Home Assistant"]
    end

    YAML --> Bridge
    UART1 --> GEA3
    UART2 --> GEA2
    GEA2 --> ADAPTER
    ADAPTER --> GEA3
    MQTT --> ADAPT

    HSM --> DIR
    HSM --> DID
    HSM --> FBM
    HSM --> POLL
    HSM --> SUB
    HSM --> WRITE

    DIR --> GEA3
    DIR --> GEA2

    DID --> GEA3

    FBM --> GEA3

    POLL --> GEA3
    SUB --> GEA3
    WRITE --> GEA3

    POLL --> CACHE
    SUB --> CACHE

    CACHE --> PUB
    PUB --> ADAPT

    WRITE --> ADAPT

    GEA3 <--> APPLIANCE
    GEA2 <--> APPLIANCE

    ADAPT <--> MQTT
    MQTT <--> HA

    classDef framework fill:#e1f5fe,stroke:#01579b
    classDef bridge fill:#f3e5f5,stroke:#4a148c
    classDef protocol fill:#e8f5e9,stroke:#1b5e20
    classDef data fill:#fff3e0,stroke:#e65100
    classDef publish fill:#fce4ec,stroke:#880e4f
    classDef external fill:#f5f5f5,stroke:#616161

    class ESPHome,YAML,MQTT,UART1,UART2 framework
    class Bridge,HSM,DIR,DID,FBM,CACHE bridge
    class Protocol,GEA3,GEA2,ADAPTER protocol
    class Bridges,POLL,SUB,WRITE data
    class Publish,PUB,ADAPT publish
    class External,APPLIANCE,HA external
```

## Module Overview

### Startup HSM

Drives the linear startup sequence: protocol stack → autodiscovery → device ID → MQTT client init → feature bits → bridge init → subscription watch → running. Uses `container_of` pattern for per-instance bridge services.

### Autodiscovery Manager

Self-driving broadcast discovery on GEA3/GEA2 bus. Finds the appliance host address and active protocol. Retries indefinitely.

### Device Identity Manager

Reads appliance type, model number, and serial number ERDs. Assembles a unique device ID string for MQTT topics.

### Feature Bit Manager

Reads and parses appliance API feature bit ERDs (0x0092–0x010D). Produces a filtered ERD set for polling mode. Falls back to full polling if ERD 0x0092 fails.

### ERD Cache

Fixed-size cache (200 entries) with inline storage for small ERDs (≤4 bytes) and heap for larger ones. Change detection at update time. Rate-limited publishing.

### Polling Bridge

Probes a pre-built ERD list, then settles into steady-state polling with budgeted read cycles. Handles appliance loss recovery.

### Subscription Bridge

Manages GEA3 ERD subscription lifecycle. Retains subscription every 30 seconds. Handles host restart detection.

### Write Bridge

Relays MQTT write requests to the ERD client. Two-state HSM (ready/writing) prevents concurrent writes.

### ERD Cache MQTT Publisher

Drains updated ERD cache entries to MQTT. On ESP-IDF, runs in a FreeRTOS background task. On non-ESP-IDF, runs in the main loop with budget parameters.

### ESPHome MQTT Client Adapter

Adapts ESPHome's MQTT client to the `i_mqtt_client` interface. Handles wildcard write topic subscription, pending update queue during disconnects, and hex payload formatting.

## Data Flow

```mermaid
graph LR
    APPLIANCE["Appliance"] --> GEA3["GEA3/GEA2<br/>Protocol Stack"]
    GEA3 --> BRIDGES["Polling / Subscription<br/>Bridge"]
    BRIDGES --> CACHE["ERD Cache"]
    CACHE --> PUB["MQTT Publisher"]
    PUB --> MQTT["MQTT Broker"]
    MQTT --> HA["Home Assistant"]

    HA2["Home Assistant"] --> MQTT2["MQTT Broker"]
    MQTT2 --> WRITE["Write Bridge"]
    WRITE --> GEA32["GEA3/GEA2<br/>Protocol Stack"]
    GEA32 --> APPLIANCE2["Appliance"]

    classDef appliance fill:#f5f5f5,stroke:#616161
    classDef protocol fill:#e8f5e9,stroke:#1b5e20
    classDef bridge fill:#fff3e0,stroke:#e65100
    classDef cache fill:#f3e5f5,stroke:#4a148c
    classDef mqtt fill:#e1f5fe,stroke:#01579b
    classDef ha fill:#fce4ec,stroke:#880e4f

    class APPLIANCE,APPLIANCE2 appliance
    class GEA3,GEA32 protocol
    class BRIDGES,WRITE bridge
    class CACHE cache
    class MQTT,MQTT2 mqtt
    class HA,HA2 ha
```

### Read Path (Appliance → Home Assistant)

1. **Protocol Stack** receives ERD data from the appliance via UART
2. **Polling/Subscription Bridge** processes the data and writes to the **ERD Cache**
3. **ERD Cache** detects changes and marks entries as `update_required`
4. **MQTT Publisher** drains updated entries and publishes hex values to MQTT topics
5. **Home Assistant** receives the MQTT messages via the ESPHome integration

### Write Path (Home Assistant → Appliance)

1. **Home Assistant** publishes a write command to `geappliances/{device_id}/erd/0x{ERD}/write`
2. **MQTT Adapter** receives the command via wildcard subscription and fires `on_write_request` event
3. **Write Bridge** forwards the write to the **ERD Client** and reports the result back to MQTT
