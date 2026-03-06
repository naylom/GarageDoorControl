# GarageControl - Architecture & Refactoring Plan

## 1. Project Overview

_High-level description of what this project does, the hardware it runs on, and the problem it solves._

- Target board: Arduino MKR WiFi 1010 (SAMD21 Cortex-M0+)
- Build system: PlatformIO
- Current version: 1.0.17 Beta
- This project creates an application that has two different components, the first deals with weather data and captures information such as temperature, humidity and presssure. The second deals with a garage door status & control. The code can be configured to run one or both of these two components. It uses a network connection to distribute real time updates on for each component that is configured. It also can received commands over the network to control the garage door features and to restart the applicationb as required. Since network credentials are required, the application will at startup look for the credentials in its flash storage and if not present start a WiFI access point with a captive WiFi feature  that allows the use to configure the app. After these are captured and stored the application restarts.
The current project is designed to interface with a Hormmann UAP garage door control (see page 3 of f:/Users/Mark%20Naylor/Downloads/Universal-Adapterplatine_UAP1.pdf ). This provides 4 status signals, Door Open, Door Closed, Door Stopped, Lamp Status and can accept 3 signals that act as commands - Close, Open, Toggle Light. Note this has its own 24V power and this is also wused to send a signal to a momentary door switch which is fed back into the arduino (after reducing to 3.3V) to provide a manual door control.
---

## 2. Current Architecture Analysis

### 2.1 Module Inventory

_Description of every source file, what it currently does, and what it owns._

| File | Responsibility |
|------|----------------|
| `main.cpp` |  Start point of application contains arduino setup() and loop() functions|
| `DoorState.h/cpp` | Class to handle Garage Door controls / status |
| `WiFiService.h/cpp` | Arduino class to encapsulate UDP data service over WiFi |
| `Display.h/cpp` | Contains most of code controlling dispay of application status |
| `ConfigStorage.h/cpp` | a class to staore ans retrieve data from flash memory, data is preserved across restarts but not across new sketch uploads. Persists and retrieves configuration using BlobStorage library.|
| `InputPin.h/cpp` | a class to handle digital pin signals, uses ISR's, keeps stats and debounces signal|
| `OutputPin.h/cpp` | a class to handle setting the value of digial pins |
| `FixedIPList.h/cpp` | Simple class to maintain a list of IP addresses|
| `logging.h/cpp` | A logging class that supports a serial or telnet based logger, also supports VT200 stylec ommands to format the screen |
| `OnboardingServer.h/cpp` | This is part of a library that supports the capture of configuaration information via an access point server and captive wifi |

### 2.2 External Library Dependencies

_List of external libraries used, their purpose, and any coupling concerns._

| Library | Purpose | Concern |
|---------|---------|---------|
| `WiFiNINA` | Standard Arduino library for WiFi Nina module on MKR WiFi 1010 | None |
| `BME280` / `BME280I2C` | Standard Arduino library for BME280 Temperature / Humidity / Pressure sensor | None except we should abstract so that other sensors can be used in the future |
| `FlashStorage_SAMD` | Underlying library used to store config data | None |
| `MNTimerLib` | Preferred library if a timer is needed | Max 8 cocncurrent timers. Uses pin A3 so avoid use otherwise |
| `MNRGBLEDBaseLib` | Preferred library for handling internal MKR WiFi LED and external RGB LEDs | None |
| `MNPCIHandler` | Preferred libabry to handle interrupts on SAMD Pins if arduinno list is insufficient | None |
| `OnboardingServer` | Preferred library to capture config data via Captive WiFI / Access point | None |

### 2.3 Key Design Issues (As-Is)

_Problems identified in the current organic design that motivate the refactoring._

- [ ] `main.cpp` - would like to see it automatically determine which, if any, Garage control / weather interfaces are present rather than be a compile time config.
- [ ] Look to reduce this and move associated #defines to PIO config file
- [ ] Minimise use of global variables
- [ ] reduce coupling between display logic and business logic
- [ ] Minimise configuration loading scattered across files

### 2.4 Data Flow Diagram (As-Is)

_Describe or diagram how data flows between modules today._

---

## 3. Target Architecture

### 3.1 Design Goals

_What the new architecture should achieve._

- [ ] Clear separation of concerns
- [ ] Testable components (no business logic in ISRs or `main.cpp`)
- [ ] No raw owning pointers — use references or stack allocation where possible
- [ ] Compile-time feature selection isolated to a single location (PIO config file)
- [ ] Consistent use of interfaces (abstract base classes) for hardware dependencies
- [ ] Use existing class libraries for Onboarding configuration information, LED manipulation, Timers.
  - The existing classes to handle pins via interrupts rather than polling is to be kept.
  - Generally having processing loops is reserved for the initial configuration capture logic and subsequent status screen updates / handling command requests.
  - In the setup detect Hormann UAP 1 presence at runtime using pin configuration - NOT_A_PIN means not wired.
  - In the setup automatically determine which weather sensors are present rather than be a config item.
  - Initially we may only have code for one type of Garage Door Control (Hormann UAP) or weather sensor (BME280) but code should be extensible so others can be added in the future.
  - Would like to have a Hormann UAP1 Class that encapsulates this interface with an extension or sub class to represent the additionl manual door control switch.
  - Need to be able to cope with WiFi reconnection robustly. Goal is the unit is low maintenance and does not get stuck in a non operational state.
  - Status screen should show appropriate information - if no garage door then just indicate that - not lots of zero door stats. Should show current time in stats and software version that is running.

### 3.2 Proposed Module Structure

Need an abstract GarageDoor class that has generic interfaces such as Open / Close / Lamp On  / Lamp off. Support querying door state (Open / Opening / Closed / Closing / Unknown / Stopped ), Lamp State ( On / Off / unknown ). Have optional call back feature so class can inform when status has changed.
Need a HormannUAP1 than implements the abstract class.
Need a class to encapsulate the UDP messages that can be sent and received.
need a class to handle the protocol of messages (ie what response to commands, some might require a UDP response, others an action to be done)
HumidityDisplay that mmaps humidity value to LED colour and flash status

#### Core Application
- `Application` class — owns top-level lifecycle (`setup` / `loop` delegation)

#### Hardware Abstraction Layer (HAL)
- `IEnvironmentSensor` — interface for BME280 / future sensors
- `IGarageDoor` — interface for GarageDoor

#### Domain Layer
- `HormannUAP1` — state machine for garage door (UAP commands, state tracking)
- `BME280Sensor` — reads and caches sensor data
- `HumidityDisplay` — maps humidity to LED colour

#### Infrastructure Layer
- `WiFiService` — WiFi connection, UDP messaging, NTP time
- `OnboardingService` — captive portal, credential storage
- `ConfigStore` — typed config read/write (wraps FlashStorage_SAMD)
- `Logger` — logging abstraction (Serial, Telnet, ANSI)

#### Messaging
- `MessageBuilder` — constructs UDP response payloads
- `CommandDispatcher` — routes incoming UDP commands to domain handlers

### 3.3 Dependency Rules

_Which layers may depend on which. Lower layers must not know about higher ones._

```
Application
    └── Domain Layer
            └── HAL interfaces (not concrete implementations)
    └── Infrastructure Layer
            └── HAL interfaces
HAL concrete classes implement HAL interfaces
```

### 3.4 Feature Flag Strategy

UAP_SUPPORT and BME280_SUPPORT are eliminated in favour of runtime detection. Debug/Logging (MNDEBUG /TELNET) move to platformio.ini build_flags are kept as the single location for compile-time config.

---

## 4. Interface Definitions

_Agreed abstract base classes / pure virtual interfaces. These are design targets — actual C++ headers are written in Phase 3._

### 4.1 `IGarageDoor`

Abstracts any garage door controller. First implementation: `HormannUAP1`.

```cpp
class IGarageDoor {
public:
    enum class State      : uint8_t { Open, Opening, Closed, Closing, Stopped, Unknown, Bad };
    enum class LightState : uint8_t { On, Off, Unknown };
    using StateChangedCallback = void (*)(State newState);

    virtual ~IGarageDoor() = default;

    // false if all pins are NOT_A_PIN (hardware not wired)
    virtual bool IsPresent() const = 0;

    // Called from Application::loop() — drives internal state machine
    virtual void Update() = 0;

    // State queries
    virtual State      GetState()               const = 0;
    virtual LightState GetLightState()          const = 0;
    virtual bool       IsOpen()                 const = 0;
    virtual bool       IsClosed()               const = 0;
    virtual bool       IsMoving()               const = 0;
    virtual bool       IsLit()                  const = 0;
    virtual const char* GetStateDisplayString() const = 0;

    // Commands
    virtual void Open()     = 0;
    virtual void Close()    = 0;
    virtual void Stop()     = 0;
    virtual void LightOn()  = 0;
    virtual void LightOff() = 0;

    virtual void SetStateChangedCallback(StateChangedCallback cb) = 0;
};
```

### 4.2 `IEnvironmentSensor`

Abstracts any environment sensor. First implementation: `BME280`.

```cpp
struct EnvironmentReading {
    float    temperature;   // Celsius
    float    humidity;      // %RH
    float    pressure;      // hPa, altitude-compensated to sea level
    float    dewpoint;      // Celsius
    uint32_t timestampMs;   // millis() at time of reading
    bool     valid;         // false until first successful read
};

class IEnvironmentSensor {
public:
    // altitudeMeters — used for sea-level pressure compensation; ignored by sensors that don't need it
    explicit IEnvironmentSensor(float altitudeMeters = 0.0f) : m_altitude(altitudeMeters) {}
    virtual ~IEnvironmentSensor() = default;

    // Probes hardware (e.g. I2C scan) — call once in setup()
    virtual bool IsPresent() = 0;
    virtual bool Begin()     = 0;

    // Populates result; returns false if sensor not ready
    virtual bool Read(EnvironmentReading& result) = 0;

protected:
    float m_altitude;  // metres above sea level, loaded from ConfigStore
};
```

### 4.3 `IMessageProtocol`

Decouples UDP message format and command dispatch from WiFi transport.

```cpp
class IMessageProtocol {
public:
    virtual ~IMessageProtocol() = default;

    // Returns the UDP payload string for a given message type, or "" if no response needed
    virtual String BuildResponse(uint8_t msgType) = 0;

    // Executes any side-effect for a command (open door, light on, etc.)
    virtual void HandleCommand(uint8_t msgType) = 0;
};
```

### 4.4 Interfaces NOT needed

| What | Why no new interface needed |
|------|--------------------------|
| `ILEDDisplay` | `MNRGBLEDBaseLib` is already an abstract base class — use it directly via pointer |
| `IInputPin` / `IOutputPin` | Existing classes are kept as-is (Decision 3); abstraction only needed if unit tests demand it |

---

## 5. Refactoring Phases

Each phase must: (a) compile cleanly, (b) produce identical runtime behaviour to the phase before it.

### Phase 1 — Structural Cleanup
**Goal:** Eliminate technical debt without touching logic. Lowest risk, highest readability gain.
- Scope:
  - Consolidate all pin constants, port numbers and threshold values into a single `config.h`
  - Move any remaining `#define` flags to `platformio.ini` `build_flags`
- Entry criteria: `master` is at v1.0.17, tests pass on hardware
- Exit criteria: Project builds cleanly; no functional change; ARCHITECTURE.md is complete up to Phase 2
- Branch: `refactor/phase1-cleanup`

### Phase 2 — Extract Application Class
**Goal:** Reduce `main.cpp` to a thin shell; move all logic into an `Application` class.
- Scope:
  - Create `Application` class with `begin()` and `loop()` methods
  - `main.cpp` becomes: instantiate `Application`, call `begin()` / `loop()`
  - Move all globals into `Application` member variables
  - No logic changes — identical behaviour
- Entry criteria: Phase 1 merged and building
- Exit criteria: `main.cpp` is ≤ 15 lines; `Application` class builds and behaves identically
- Branch: `refactor/phase2-application-class`

### Phase 3 — Define Interfaces (Headers Only)
**Goal:** Write all abstract interface headers (Section 4) with no implementations yet.
- Scope:
  - `IGarageDoor.h` — with `State`, `LightState` enums and pure virtuals
  - `IEnvironmentSensor.h` — with `EnvironmentReading` struct and pure virtuals
  - `IMessageProtocol.h`
  - No `.cpp` files; no changes to existing concrete classes yet
- Entry criteria: Phase 2 merged
- Exit criteria: All headers compile; existing code unmodified
- Branch: `refactor/phase3-interfaces`

### Phase 4 — Implement `HormannUAP1`
**Goal:** Refactor `DoorState` into `HormannUAP1` implementing `IGarageDoor`.
- Scope:
  - Rename/refactor `DoorState` → `HormannUAP1 : public IGarageDoor`
  - Implement `IsPresent()` using `NOT_A_PIN` sentinel
  - Add `SetStateChangedCallback()` — replace multicast call directly from `main.cpp`
  - Create `HormannUAP1WithSwitch` subclass for manual switch pin
  - Update `Application` to use `IGarageDoor*`
- Entry criteria: Phase 3 merged
- Exit criteria: Builds and hardware-tests identically to v1.0.17; `DoorState.h` deleted
- Branch: `refactor/phase4-hormann-uap1`

### Phase 5 — Implement `BME280Sensor`
**Goal:** Wrap BME280 behind `IEnvironmentSensor`; enable I2C auto-detection.
- Scope:
  - Create `BME280Sensor : public IEnvironmentSensor`
  - Constructor receives `altitudeMeters` loaded from `ConfigStore` (see Decision 6)
  - `IsPresent()` probes I2C bus — replaces `BME280_SUPPORT` ifdef
  - Move `EnvironmentResults` struct into `BME280Sensor`
  - Update `Application` to: load config, construct `BME280Sensor(config.altitudeCompensation)`, use via `IEnvironmentSensor*`
- Entry criteria: Phase 4 merged
- Exit criteria: Builds; sensor detected at runtime without `#ifdef`; `BME280_SUPPORT` removed
- Branch: `refactor/phase5-environment-sensor`

### Phase 6 — Messaging Layer ✅
**Goal:** Pull `BuildMessage()` and command routing out of `Application` into a dedicated class.
- Scope:
  - Implement `GarageMessageProtocol : public IMessageProtocol`
  - `CommandDispatcher` routes incoming UDP message types to `IGarageDoor` / `IEnvironmentSensor`
  - `Application` calls `protocol.BuildResponse()` and `protocol.HandleCommand()` only
- Entry criteria: Phase 5 merged
- Exit criteria: `Application` has no knowledge of message string format; builds and tests pass
- Branch: `refactor/phase6-messaging`

### Phase 7 — Display Cleanup
**Goal:** `Display` class receives its data through interfaces, not global struct access.
- Scope:
  - `Display` constructor takes `IGarageDoor*` and `IEnvironmentSensor*`
  - Remove direct access to `EnvironmentResults` global from `Display`
  - Status screen shows "No garage door" / "No sensor" when pointers are null
  - Add current time and software version to status screen (Decision from 3.1)
- Entry criteria: Phase 6 merged
- Exit criteria: Display behaviour identical or improved; no global data access from Display
- Branch: `refactor/phase7-display`

### Phase 8 — WiFi Robustness & Final Cleanup
**Goal:** Robust WiFi reconnection; remove any remaining `#ifdef` usage; final review.
- Scope:
  - Implement reconnection strategy in `WiFiService` (exponential backoff, watchdog reset if stuck)
  - Audit and remove any remaining `#ifdef` blocks not already addressed
  - Final ARCHITECTURE.md update — mark all phases complete, record lessons learned
- Entry criteria: Phase 7 merged; hardware soak-tested
- Exit criteria: Unit survives WiFi drop/reconnect without manual restart; zero `#ifdef UAP_SUPPORT` / `#ifdef BME280_SUPPORT` remaining
- Branch: `refactor/phase8-wifi-robustness`

## 6. Decisions Log

_Record of significant architectural decisions with rationale. Add an entry for every non-obvious choice._

| # | Decision 																			| Rationale 															| Date 	 |
|---|-----------------------------------------------------------------------------------|-----------------------------------------------------------------------|--------|
| 1 | Minimise #ifdef usage 															| Makes code easier to understand 										| 6/3/26 |
| 2 | Auto detect interfaces 															| Simpler codebase 														| 6/3/26 |
| 3 | Use ISR based code for pin handling 												| Removes complicated loop polling 										| 6/3/26 |
| 4 | Abstract GarageDoor and Environment sensors so others can be used in the future 	| extensibility 														| 6/3/26 |
| 5 | Hormann UAP1 presence detection by pin configuration (NOT_A_PIN means not wired) 	| GPIO has no bus discovery; NOT_A_PIN is idiomatic Arduino sentinel 	| 6/3/26 |
| 6 | Altitude compensation passed to `IEnvironmentSensor` constructor from `ConfigStore` | Keeps sensor self-contained; ignored by sensors that don't need it; single config load point in `Application` | 6/3/26 |

---

## 7. Out of Scope

_Things explicitly decided NOT to change during this refactoring._

- Runtime behaviour must remain identical to v1.0.17
- No change to hardware pin assignments
- No change to UDP message format (wire protocol)

---

## 8. Open Questions

None currently open.

---

## 9. How to Use This Document

Start every new AI chat session with:

```
Context: I am refactoring GarageControl. The architecture plan is in ARCHITECTURE.md 
[paste relevant sections].
Today's goal: [specific task].
Constraints: Arduino C++, MKR WiFi 1010 (SAMD21), PlatformIO, must build clean.
Do NOT suggest changes outside today's scope.
```
