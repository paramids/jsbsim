# JSBSim Flight Dynamics Model Plugin - Extended Modules Documentation

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [JSBSimUDPInput Module](#jsbsimudpinput-module)
4. [JSBSimSimulationFSM Module](#jsbsimsimulationfsm-module)
5. [Data Flow Diagrams](#data-flow-diagrams)
6. [Class Diagrams](#class-diagrams)
7. [Integration Guide](#integration-guide)
8. [API Reference](#api-reference)

---

## Overview

The JSBSim Flight Dynamics Model plugin has been extended with two new runtime modules:

- **JSBSimUDPInput**: Provides bidirectional UDP communication for receiving external control data and sending simulation state
- **JSBSimSimulationFSM**: Manages simulation lifecycle states with a finite state machine

Both modules integrate seamlessly with the existing `JSBSimFlightDynamicsModel` module and provide Blueprint-accessible interfaces.

---

## Architecture

### Module Structure

```mermaid
graph TB
    subgraph Plugin["JSBSimFlightDynamicsModel Plugin"]
        subgraph Core["Core Module"]
            JSBSimModule[FJSBSimModule]
            MovementComp[UJSBSimMovementComponent]
            FDMTypes[FDMTypes]
        end
        
        subgraph UDP["JSBSimUDPInput Module"]
            UDPModule[FJSBSimUDPInputModule]
            UDPComp[UJSBSimUDPInputComponent]
            UDPMapping[FUDPDataMapping]
        end
        
        subgraph FSM["JSBSimSimulationFSM Module"]
            FSMModule[FJSBSimSimulationFSMModule]
            FSMComp[UJSBSimSimulationFSMComponent]
            FSMState[EJSBSimSimulationState]
        end
        
        subgraph Editor["Editor Module"]
            EditorModule[FJSBSimFlightDynamicsModelEditorModule]
        end
    end
    
    subgraph External["External Systems"]
        UDPClient[UDP Client/Server]
        Blueprint[Unreal Blueprints]
    end
    
    subgraph JSBSim["JSBSim Library"]
        FDMExec[FGFDMExec]
        PropertyMgr[FGPropertyManager]
        FCS[FGFCS]
    end
    
    UDPClient -->|UDP Packets| UDPComp
    UDPComp -->|Commands/Properties| MovementComp
    MovementComp -->|State| UDPComp
    UDPComp -->|State Data| UDPClient
    
    FSMComp -->|Monitors| MovementComp
    MovementComp -->|Lifecycle Events| FSMComp
    
    MovementComp -->|Direct Access| FDMExec
    MovementComp -->|Property Access| PropertyMgr
    MovementComp -->|Flight Controls| FCS
    
    Blueprint -->|Configure| UDPComp
    Blueprint -->|Configure| FSMComp
    Blueprint -->|Configure| MovementComp
    
    UDPComp -.->|Optional Integration| FSMComp
```

### Module Dependencies

```mermaid
graph LR
    JSBSimFlightDynamicsModel[JSBSimFlightDynamicsModel<br/>Core Module]
    JSBSimUDPInput[JSBSimUDPInput<br/>UDP Module]
    JSBSimSimulationFSM[JSBSimSimulationFSM<br/>FSM Module]
    JSBSimLib[JSBSim Library<br/>Third Party]
    
    JSBSimUDPInput -->|Depends on| JSBSimFlightDynamicsModel
    JSBSimSimulationFSM -->|Depends on| JSBSimFlightDynamicsModel
    JSBSimSimulationFSM -->|Optional| JSBSimUDPInput
    JSBSimFlightDynamicsModel -->|Links| JSBSimLib
```

---

## JSBSimUDPInput Module

### Overview

The `JSBSimUDPInput` module provides bidirectional UDP communication capabilities, allowing external systems to send control commands to JSBSim and receive simulation state data.

### Key Features

- **Bidirectional Communication**: Receive control data and send state updates
- **Flexible Data Formats**: Support for JSON, Text (key-value), and Binary formats
- **Configurable Field Mappings**: Map UDP fields to JSBSim properties or command structures
- **Non-blocking Sockets**: Efficient polling-based receive/send
- **Blueprint Integration**: Fully accessible from Blueprints
- **Debug Visualization**: On-screen status display

### Component Architecture

```mermaid
graph TB
    subgraph UDPComponent["UJSBSimUDPInputComponent"]
        SocketMgr[Socket Management<br/>FSocket, ISocketSubsystem]
        DataParser[Data Parser<br/>JSON/Text/Binary]
        MappingEngine[Mapping Engine<br/>Field → JSBSim]
        Serializer[Data Serializer<br/>JSBSim → UDP]
        Config[Configuration<br/>Ports, Rates, Mappings]
    end
    
    subgraph MovementComp["UJSBSimMovementComponent"]
        Commands[FFlightControlCommands]
        AircraftState[FAircraftState]
        CommandConsole[CommandConsole<br/>Property Access]
    end
    
    subgraph JSBSimCore["JSBSim Core"]
        PropertyMgr[PropertyManager]
        FCS[Flight Control System]
    end
    
    SocketMgr -->|Raw UDP Data| DataParser
    DataParser -->|Parsed Key-Value| MappingEngine
    MappingEngine -->|Direct Assignment| Commands
    MappingEngine -->|Property Set| CommandConsole
    CommandConsole -->|Property Access| PropertyMgr
    Commands -->|Next Tick| FCS
    
    AircraftState -->|State Data| Serializer
    CommandConsole -->|Property Values| Serializer
    Serializer -->|Serialized Data| SocketMgr
```

### Data Mapping System

The module uses a flexible mapping system to translate between UDP fields and JSBSim properties/commands:

```mermaid
graph LR
    subgraph UDPData["UDP Packet"]
        UDPField1[Field: Aileron<br/>Value: 0.5]
        UDPField2[Field: Throttle<br/>Value: 0.8]
    end
    
    subgraph Mapping["Mapping Configuration"]
        Map1[Mapping 1<br/>UDP: Aileron<br/>Target: Command<br/>Member: Aileron]
        Map2[Mapping 2<br/>UDP: Throttle<br/>Target: Property<br/>Path: propulsion/engine/throttle]
    end
    
    subgraph JSBSimTarget["JSBSim Target"]
        Commands[Commands.Aileron = 0.5]
        Property[propulsion/engine/throttle = 0.8]
    end
    
    UDPField1 -->|Apply Mapping| Map1
    UDPField2 -->|Apply Mapping| Map2
    Map1 -->|Direct Assignment| Commands
    Map2 -->|CommandConsole| Property
```

### Mapping Target Types

1. **Property**: Direct JSBSim property path (e.g., `propulsion/engine/throttle`)
2. **Command**: Flight control command structure member (e.g., `Aileron`, `Elevator`)
3. **Custom**: Extensible for custom use cases

---

## JSBSimSimulationFSM Module

### Overview

The `JSBSimSimulationFSM` module provides a finite state machine to track and manage the simulation lifecycle, monitoring the `JSBSimMovementComponent` and providing state transition events.

### State Machine

```mermaid
stateDiagram-v2
    [*] --> Uninitialized
    Uninitialized --> Initializing: InitializeJSBSim()
    Initializing --> Loading: LoadAircraft()
    Loading --> Preparing: PrepareJSBSim()
    Preparing --> Ready: Trim Complete
    Ready --> Running: Simulation Starts
    Running --> Paused: Pause Request
    Paused --> Running: Resume Request
    Running --> Error: Error Detected
    Preparing --> Error: Error Detected
    Loading --> Error: Error Detected
    Initializing --> Error: Error Detected
    Error --> Uninitialized: Reset
    Running --> Stopping: Shutdown
    Paused --> Stopping: Shutdown
    Error --> Stopping: Shutdown
    Stopping --> [*]
    
    note right of Uninitialized
        JSBSim not initialized
        No aircraft loaded
    end note
    
    note right of Ready
        Aircraft loaded and trimmed
        Ready to run simulation
    end note
    
    note right of Running
        Simulation actively running
        JSBSim ticking
    end note
```

### State Monitoring

```mermaid
graph TB
    subgraph FSMComponent["UJSBSimSimulationFSMComponent"]
        StateMonitor[State Monitor<br/>TickComponent]
        TransitionMgr[Transition Manager<br/>Validation & Events]
        StateHistory[State History<br/>Transition Log]
    end
    
    subgraph MovementComp["UJSBSimMovementComponent"]
        Flags[JSBSimInitialized<br/>AircraftLoaded<br/>Trimmed]
        TickStatus[Component Tick Status]
    end
    
    subgraph Events["Blueprint Events"]
        OnStateChanged[OnStateChanged]
        OnStateEntered[OnStateEntered]
        OnStateExited[OnStateExited]
    end
    
    StateMonitor -->|Poll Flags| Flags
    StateMonitor -->|Check Status| TickStatus
    StateMonitor -->|Determine State| TransitionMgr
    TransitionMgr -->|Validate| TransitionMgr
    TransitionMgr -->|Record| StateHistory
    TransitionMgr -->|Broadcast| OnStateChanged
    TransitionMgr -->|Broadcast| OnStateEntered
    TransitionMgr -->|Broadcast| OnStateExited
```

---

## Data Flow Diagrams

### UDP Input Flow (External → JSBSim)

```mermaid
sequenceDiagram
    participant External as External System
    participant UDP as UDP Component
    participant Parser as Data Parser
    participant Mapper as Mapping Engine
    participant Movement as Movement Component
    participant JSBSim as JSBSim Core
    
    External->>UDP: Send UDP Packet<br/>(JSON/Text/Binary)
    UDP->>UDP: Receive on Socket<br/>(Non-blocking)
    UDP->>Parser: Raw Data (TArray<uint8>)
    
    alt JSON Format
        Parser->>Parser: FJsonSerializer::Deserialize()
    else Text Format
        Parser->>Parser: Split by "="
    end
    
    Parser->>Mapper: Parsed Data<br/>(TMap<FString, FString>)
    
    loop For each Input Mapping
        alt Mapping Target: Command
            Mapper->>Movement: Commands.Aileron = value<br/>(Direct Assignment)
            Note over Movement: Commands structure updated
        else Mapping Target: Property
            Mapper->>Movement: CommandConsole(property, value)
            Movement->>JSBSim: PropertyManager->setStringValue()
            Note over JSBSim: Property directly updated
        end
    end
    
    Note over Movement,JSBSim: Next Tick: CopyToJSBSim()<br/>transfers Commands to FCS
```

### UDP Output Flow (JSBSim → External)

```mermaid
sequenceDiagram
    participant JSBSim as JSBSim Core
    participant Movement as Movement Component
    participant Collector as Output Collector
    participant Serializer as Data Serializer
    participant UDP as UDP Component
    participant External as External System
    
    Note over JSBSim: Simulation Running
    
    UDP->>UDP: TickComponent()<br/>(Send Timer)
    UDP->>Collector: CollectOutputData()
    
    loop For each Output Mapping
        alt Source Type: Property
            Collector->>Movement: CommandConsole(property, "")
            Movement->>JSBSim: PropertyManager->getStringValue()
            JSBSim-->>Movement: Property Value
        else Source Type: Command
            Collector->>Movement: Read AircraftState or Commands
            Movement-->>Collector: State/Command Value
        end
    end
    
    Collector->>Serializer: Output Data<br/>(TMap<FString, FString>)
    
    alt JSON Format
        Serializer->>Serializer: FJsonSerializer::Serialize()
    else Text Format
        Serializer->>Serializer: Format as "key=value\n"
    end
    
    Serializer->>UDP: Serialized Data<br/>(TArray<uint8>)
    UDP->>External: Send UDP Packet
```

### FSM State Transition Flow

```mermaid
sequenceDiagram
    participant FSM as FSM Component
    participant Monitor as State Monitor
    participant Validator as Transition Validator
    participant History as State History
    participant Events as Blueprint Events
    participant Movement as Movement Component
    
    FSM->>Monitor: TickComponent()
    Monitor->>Movement: Check JSBSimInitialized<br/>AircraftLoaded<br/>Trimmed
    Movement-->>Monitor: Current Flags
    
    Monitor->>Monitor: Determine Target State
    
    alt State Change Needed
        Monitor->>Validator: Request Transition<br/>(From → To)
        Validator->>Validator: Validate Transition
        
        alt Valid Transition
            Validator->>FSM: Update CurrentState
            FSM->>History: Record Transition
            FSM->>Events: Broadcast OnStateExited(Old)
            FSM->>Events: Broadcast OnStateEntered(New)
            FSM->>Events: Broadcast OnStateChanged(New, Old)
        else Invalid Transition
            Validator->>FSM: Log Warning<br/>(No Transition)
        end
    end
```

---

## Class Diagrams

### Module Class Hierarchy

```mermaid
classDiagram
    class IModuleInterface {
        <<interface>>
        +StartupModule()
        +ShutdownModule()
    }
    
    class FJSBSimModule {
        +StartupModule()
        +ShutdownModule()
    }
    
    class FJSBSimUDPInputModule {
        +StartupModule()
        +ShutdownModule()
    }
    
    class FJSBSimSimulationFSMModule {
        +StartupModule()
        +ShutdownModule()
    }
    
    IModuleInterface <|.. FJSBSimModule
    IModuleInterface <|.. FJSBSimUDPInputModule
    IModuleInterface <|.. FJSBSimSimulationFSMModule
```

### Component Class Diagram

```mermaid
classDiagram
    class UActorComponent {
        <<UE Base>>
        +BeginPlay()
        +EndPlay()
        +TickComponent()
    }
    
    class UJSBSimMovementComponent {
        -JSBSim::FGFDMExec* Exec
        -bool JSBSimInitialized
        -bool AircraftLoaded
        -bool Trimmed
        +FFlightControlCommands Commands
        +FAircraftState AircraftState
        +CommandConsole(property, value)
        +LoadAircraft()
        +PrepareJSBSim()
        +CopyToJSBSim()
        +CopyFromJSBSim()
    }
    
    class UJSBSimUDPInputComponent {
        -FSocket* ReceiveSocket
        -FSocket* SendSocket
        -ISocketSubsystem* SocketSubsystem
        -FUDPDataMappingConfig DataMappingConfig
        +UJSBSimMovementComponent* TargetMovementComponent
        +int32 LocalPort
        +int32 RemotePort
        +FString RemoteIP
        +bool bEnableReceiving
        +bool bEnableSending
        +float SendRateHz
        +StartUDPListening()
        +StopUDPListening()
        +SendUDPData()
        +ProcessUDPData()
        -ProcessReceivedData()
        -ApplyInputMappings()
        -SendJSBSimState()
        -ParseReceivedData()
        -SerializeOutputData()
    }
    
    class UJSBSimSimulationFSMComponent {
        -EJSBSimSimulationState CurrentState
        -TArray~FJSBSimStateTransition~ StateHistory
        +UJSBSimMovementComponent* TargetMovementComponent
        +bool bAutoMonitorMovementComponent
        +bool bAllowManualTransitions
        +GetCurrentState()
        +RequestStateTransition()
        +PauseSimulation()
        +ResumeSimulation()
        +ResetSimulation()
        -TransitionToState()
        -MonitorMovementComponent()
        -CheckForErrors()
    }
    
    UActorComponent <|-- UJSBSimMovementComponent
    UActorComponent <|-- UJSBSimUDPInputComponent
    UActorComponent <|-- UJSBSimSimulationFSMComponent
    
    UJSBSimUDPInputComponent --> UJSBSimMovementComponent : references
    UJSBSimSimulationFSMComponent --> UJSBSimMovementComponent : references
    UJSBSimUDPInputComponent ..> UJSBSimSimulationFSMComponent : optional integration
```

### Data Structure Class Diagram

```mermaid
classDiagram
    class FUDPDataMappingConfig {
        +EUDPDataFormat DataFormat
        +TArray~FUDPFieldMapping~ InputMappings
        +TArray~FUDPOutputMapping~ OutputMappings
        +bool bEnableInputMapping
        +bool bEnableOutputMapping
    }
    
    class FUDPFieldMapping {
        +FString UDPFieldName
        +EUDPMappingTarget MappingTarget
        +FString JSBSimPropertyPath
        +FString CommandMemberName
        +bool bEnabled
    }
    
    class FUDPOutputMapping {
        +FString UDPFieldName
        +EUDPMappingTarget SourceType
        +FString JSBSimPropertyPath
        +FString StateMemberName
        +bool bEnabled
    }
    
    class EJSBSimSimulationState {
        <<enumeration>>
        Uninitialized
        Initializing
        Loading
        Preparing
        Ready
        Running
        Paused
        Stopping
        Error
    }
    
    class FJSBSimStateTransition {
        +EJSBSimSimulationState FromState
        +EJSBSimSimulationState ToState
        +double Timestamp
    }
    
    class FFlightControlCommands {
        +double Aileron
        +double Elevator
        +double Rudder
        +double YawTrim
        +double PitchTrim
        +double RollTrim
        +double Flap
        +double GearDown
        +double LeftBrake
        +double RightBrake
        ...
    }
    
    class FAircraftState {
        +double CalibratedAirSpeedKts
        +double GroundSpeedKts
        +double AltitudeASLFt
        +double AltitudeAGLFt
        +FVector ECEFLocation
        +FRotator LocalEulerAngles
        ...
    }
    
    FUDPDataMappingConfig *-- FUDPFieldMapping : contains
    FUDPDataMappingConfig *-- FUDPOutputMapping : contains
    UJSBSimSimulationFSMComponent *-- EJSBSimSimulationState : uses
    UJSBSimSimulationFSMComponent *-- FJSBSimStateTransition : contains
    UJSBSimMovementComponent *-- FFlightControlCommands : contains
    UJSBSimMovementComponent *-- FAircraftState : contains
```

### Dependency Graph

```mermaid
graph TD
    subgraph Core["Core Module"]
        MovementComp[UJSBSimMovementComponent]
        FDMTypes[FDMTypes]
        JSBSimModule[FJSBSimModule]
    end
    
    subgraph UDP["UDP Module"]
        UDPComp[UJSBSimUDPInputComponent]
        UDPMapping[FUDPDataMapping]
        UDPModule[FJSBSimUDPInputModule]
    end
    
    subgraph FSM["FSM Module"]
        FSMComp[UJSBSimSimulationFSMComponent]
        FSMState[EJSBSimSimulationState]
        FSMModule[FJSBSimSimulationFSMModule]
    end
    
    subgraph External["External Dependencies"]
        Sockets[Sockets Module]
        Networking[Networking Module]
        Json[Json Module]
        Engine[Engine Module]
    end
    
    subgraph JSBSim["JSBSim Library"]
        FDMExec[FGFDMExec]
        PropertyMgr[FGPropertyManager]
    end
    
    UDPComp --> MovementComp
    UDPComp --> UDPMapping
    UDPComp --> FDMTypes
    UDPModule --> UDPComp
    
    FSMComp --> MovementComp
    FSMComp --> FSMState
    FSMModule --> FSMComp
    
    MovementComp --> FDMExec
    MovementComp --> PropertyMgr
    MovementComp --> FDMTypes
    
    UDPComp --> Sockets
    UDPComp --> Networking
    UDPComp --> Json
    UDPComp --> Engine
    
    FSMComp --> Engine
    
    FSMComp -.->|Optional| UDPComp
```

---

## Integration Guide

### Basic Setup

1. **Add Components to Actor**
   ```cpp
   // In your Actor class or Blueprint
   UJSBSimMovementComponent* MovementComponent;
   UJSBSimUDPInputComponent* UDPComponent;
   UJSBSimSimulationFSMComponent* FSMComponent;
   ```

2. **Configure UDP Component**
   ```cpp
   // Set target movement component
   UDPComponent->TargetMovementComponent = MovementComponent;
   
   // Configure ports
   UDPComponent->LocalPort = 8889;  // Receive port
   UDPComponent->RemotePort = 8890; // Send port
   UDPComponent->RemoteIP = TEXT("127.0.0.1");
   
   // Configure data format
   UDPComponent->DataMappingConfig.DataFormat = EUDPDataFormat::JSON;
   
   // Start listening
   UDPComponent->StartUDPListening();
   ```

3. **Configure FSM Component**
   ```cpp
   // Set target movement component
   FSMComponent->TargetMovementComponent = MovementComponent;
   
   // Enable auto-monitoring
   FSMComponent->bAutoMonitorMovementComponent = true;
   
   // Enable state transition logging
   FSMComponent->bLogStateTransitions = true;
   ```

### Configuring Data Mappings

#### Input Mapping Example (UDP → JSBSim)

```cpp
// Create input mapping for Aileron command
FUDPFieldMapping AileronMapping;
AileronMapping.UDPFieldName = TEXT("Aileron");
AileronMapping.MappingTarget = EUDPMappingTarget::Command;
AileronMapping.CommandMemberName = TEXT("Aileron");
AileronMapping.bEnabled = true;

// Create input mapping for direct property
FUDPFieldMapping ThrottleMapping;
ThrottleMapping.UDPFieldName = TEXT("Throttle");
ThrottleMapping.MappingTarget = EUDPMappingTarget::Property;
ThrottleMapping.JSBSimPropertyPath = TEXT("propulsion/engine/throttle");
ThrottleMapping.bEnabled = true;

// Add to configuration
UDPComponent->DataMappingConfig.InputMappings.Add(AileronMapping);
UDPComponent->DataMappingConfig.InputMappings.Add(ThrottleMapping);
```

#### Output Mapping Example (JSBSim → UDP)

```cpp
// Create output mapping for airspeed
FUDPOutputMapping AirspeedMapping;
AirspeedMapping.UDPFieldName = TEXT("AirspeedKts");
AirspeedMapping.SourceType = EUDPMappingTarget::Command;
AirspeedMapping.StateMemberName = TEXT("CalibratedAirSpeedKts");
AirspeedMapping.bEnabled = true;

// Create output mapping for direct property
FUDPOutputMapping AltitudeMapping;
AltitudeMapping.UDPFieldName = TEXT("AltitudeFt");
AltitudeMapping.SourceType = EUDPMappingTarget::Property;
AltitudeMapping.JSBSimPropertyPath = TEXT("position/altitude-ft");
AltitudeMapping.bEnabled = true;

// Add to configuration
UDPComponent->DataMappingConfig.OutputMappings.Add(AirspeedMapping);
UDPComponent->DataMappingConfig.OutputMappings.Add(AltitudeMapping);
```

### Blueprint Integration

Both components are fully Blueprint-accessible:

- **Properties**: All configuration properties are `BlueprintReadWrite`
- **Functions**: All public functions are `BlueprintCallable`
- **Events**: All delegates are `BlueprintAssignable`

### Event Handling

#### UDP Component Events

```cpp
// Bind to data received event
UDPComponent->OnUDPDataReceived.AddDynamic(this, &AMyActor::OnUDPDataReceived);

void AMyActor::OnUDPDataReceived(const TArray<uint8>& Data, const FString& SenderAddress)
{
    // Handle received data
}
```

#### FSM Component Events

```cpp
// Bind to state changed event
FSMComponent->OnStateChanged.AddDynamic(this, &AMyActor::OnSimulationStateChanged);

void AMyActor::OnSimulationStateChanged(EJSBSimSimulationState NewState, EJSBSimSimulationState PreviousState)
{
    // Handle state transition
    if (NewState == EJSBSimSimulationState::Running)
    {
        // Simulation started
    }
}
```

---

## API Reference

### UJSBSimUDPInputComponent

#### Configuration Properties

| Property | Type | Description |
|----------|------|-------------|
| `TargetMovementComponent` | `UJSBSimMovementComponent*` | Target JSBSim component to send data to |
| `LocalPort` | `int32` | Local port to receive UDP packets (default: 8889) |
| `RemotePort` | `int32` | Remote port to send UDP packets (default: 8890) |
| `RemoteIP` | `FString` | Remote IP address (empty = broadcast) |
| `bAutoStart` | `bool` | Automatically start listening on BeginPlay |
| `bEnableReceiving` | `bool` | Enable receiving UDP data |
| `bEnableSending` | `bool` | Enable sending UDP data |
| `SendRateHz` | `float` | Send rate in Hz (0 = every tick, default: 60) |
| `DataMappingConfig` | `FUDPDataMappingConfig` | Data mapping configuration |

#### Functions

- `bool StartUDPListening()` - Start UDP listening on configured port
- `void StopUDPListening()` - Stop UDP listening and cleanup sockets
- `bool SendUDPData(const TArray<uint8>& Data)` - Send raw UDP data
- `void ProcessUDPData(const TArray<uint8>& Data)` - Process received UDP data
- `void UpdateDataMappingConfig(const FUDPDataMappingConfig& NewConfig)` - Update mapping configuration
- `bool GetIsConnected() const` - Get current connection status

#### Events

- `FOnUDPDataReceived` - Fired when UDP data is received
- `FOnUDPDataSent` - Fired when UDP data is sent
- `FOnUDPConnectionStatusChanged` - Fired when connection status changes

### UJSBSimSimulationFSMComponent

#### Configuration Properties

| Property | Type | Description |
|----------|------|-------------|
| `TargetMovementComponent` | `UJSBSimMovementComponent*` | Target JSBSim component to monitor |
| `bAutoMonitorMovementComponent` | `bool` | Automatically monitor component lifecycle |
| `bAllowManualTransitions` | `bool` | Allow manual state transitions |
| `bLogStateTransitions` | `bool` | Enable logging of state transitions |
| `MaxHistorySize` | `int32` | Maximum state transition history size (default: 100) |

#### State Properties

| Property | Type | Description |
|----------|------|-------------|
| `CurrentState` | `EJSBSimSimulationState` | Current simulation state (read-only) |
| `StateHistory` | `TArray<FJSBSimStateTransition>` | State transition history (read-only) |

#### Functions

- `EJSBSimSimulationState GetCurrentState() const` - Get current state
- `bool RequestStateTransition(EJSBSimSimulationState NewState)` - Request state transition
- `bool PauseSimulation()` - Pause simulation (transition to Paused)
- `bool ResumeSimulation()` - Resume simulation (transition to Running)
- `bool ResetSimulation()` - Reset to Uninitialized state
- `void UpdateStateFromMovementComponent()` - Manually trigger state monitoring
- `FJSBSimStateTransition GetLastTransition() const` - Get last state transition
- `bool IsValidTransition(EJSBSimSimulationState From, EJSBSimSimulationState To) const` - Check if transition is valid

#### Events

- `FOnStateChanged` - Fired when state changes (provides new and previous state)
- `FOnStateEntered` - Fired when entering a specific state
- `FOnStateExited` - Fired when exiting a specific state

### EJSBSimSimulationState

Enumeration of simulation states:

- `Uninitialized` - JSBSim not initialized
- `Initializing` - JSBSim initialization in progress
- `Loading` - Aircraft model loading
- `Preparing` - Preparing simulation (trimming, etc.)
- `Ready` - Simulation ready to run
- `Running` - Simulation actively running
- `Paused` - Simulation paused
- `Stopping` - Simulation shutting down
- `Error` - Error state

---

## Example Usage Scenarios

### Scenario 1: External Flight Control System

```mermaid
sequenceDiagram
    participant External as External Flight Control
    participant UDP as UDP Component
    participant JSBSim as JSBSim
    participant FSM as FSM Component
    
    External->>UDP: Send Control Commands<br/>(Aileron, Elevator, Throttle)
    UDP->>JSBSim: Update Commands Structure
    JSBSim->>JSBSim: Process Flight Dynamics
    JSBSim->>UDP: Send State Data<br/>(Airspeed, Altitude, Attitude)
    UDP->>External: Return State Data
    
    JSBSim->>FSM: Update Lifecycle Flags
    FSM->>FSM: Monitor & Update State
    FSM->>External: State Change Events<br/>(if integrated)
```

### Scenario 2: Simulation State Management

```mermaid
sequenceDiagram
    participant User as User/Blueprint
    participant FSM as FSM Component
    participant Movement as Movement Component
    participant JSBSim as JSBSim Core
    
    User->>FSM: Request Pause
    FSM->>FSM: Validate Transition
    FSM->>FSM: Transition to Paused
    FSM->>User: OnStateChanged Event
    
    User->>FSM: Request Resume
    FSM->>FSM: Validate Transition
    FSM->>FSM: Transition to Running
    FSM->>User: OnStateChanged Event
    
    Movement->>FSM: Lifecycle Changes
    FSM->>FSM: Auto-Update State
    FSM->>User: State Change Events
```

---

## Performance Considerations

### UDP Component

- **Non-blocking Sockets**: Uses non-blocking I/O to avoid blocking the game thread
- **Polling Frequency**: Receives data every tick (typically 60 Hz)
- **Send Rate Control**: Configurable send rate to limit bandwidth usage
- **Buffer Management**: Uses fixed-size receive buffer (4096 bytes)

### FSM Component

- **Polling-Based Monitoring**: Checks component state every tick
- **State History Limit**: Configurable history size to limit memory usage
- **Event Broadcasting**: Events are lightweight multicast delegates

### Recommendations

- Set appropriate send rates based on your needs (default 60 Hz is usually sufficient)
- Limit state history size if memory is a concern
- Use Blueprint events sparingly for performance-critical code
- Consider disabling debug visualization in production builds

---

## Troubleshooting

### UDP Component Issues

**Problem**: No data received
- Check that `StartUDPListening()` was called
- Verify `LocalPort` is not in use by another application
- Check firewall settings
- Enable debug visualization to see connection status

**Problem**: Data not reaching JSBSim
- Verify `TargetMovementComponent` is set
- Check that input mappings are configured correctly
- Ensure mappings are enabled (`bEnabled = true`)
- Check log output for parsing errors

**Problem**: Invalid data format
- Verify `DataFormat` matches the actual UDP packet format
- For JSON, ensure valid JSON syntax
- For Text, ensure "key=value" format with newlines

### FSM Component Issues

**Problem**: State not updating
- Verify `bAutoMonitorMovementComponent` is enabled
- Check that `TargetMovementComponent` is set
- Ensure component is ticking
- Check log output for state transition messages

**Problem**: Invalid state transitions
- Check `IsValidTransition()` to see allowed transitions
- Review state machine diagram for valid paths
- Enable `bLogStateTransitions` for debugging

---

## Future Enhancements

Potential improvements for future versions:

1. **Binary Protocol Support**: Full implementation of binary format parsing/serialization
2. **WebSocket Support**: Alternative to UDP for web-based interfaces
3. **State Machine Actions**: Execute actions on state transitions
4. **Advanced Error Recovery**: Automatic error recovery mechanisms
5. **Performance Metrics**: Built-in performance monitoring and statistics
6. **Configuration Presets**: Pre-configured mapping presets for common aircraft

---

## License

This documentation is part of the JSBSim Flight Dynamics Model plugin. See the main plugin LICENSE file for details.

---

## Version History

- **v1.01** - Initial release with UDP and FSM modules
  - Basic UDP bidirectional communication
  - JSON and Text format support
  - FSM with 9 states
  - Blueprint integration
  - Debug visualization

