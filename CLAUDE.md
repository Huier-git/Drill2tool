# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DrillControl is a Qt5-based drilling rig data acquisition and control system with a modular architecture. The system controls 9 drilling mechanism degrees of freedom (DOF) using EtherCAT and Modbus TCP protocols.

**Key Features:**
- Multi-threaded data acquisition (vibration sensors, motor parameters, Modbus sensors)
- Real-time motion control with safety interlocks
- 9 independent mechanism controllers (feed, rotation, percussion, clamp, storage, arm extension/grip/rotation, docking)
- SQLite database for sensor data logging
- JSON-based configuration for mechanism parameters

## Build Commands

### Using Qt Creator
1. Open `DrillControl.pro`
2. Configure kit (MSVC 2019 or MinGW 8.1)
3. Build (Ctrl+B)

### Command Line (Windows)

```powershell
# Generate Makefile
qmake DrillControl.pro

# Build with MSVC
nmake

# Build with MinGW
mingw32-make

# Run
cd build/debug
./DrillControl.exe
```

**Build Output:** `build/debug/` or `build/release/`

## Development Environment

- **Qt:** 5.15.2
- **Compiler:** MSVC 2019 / MinGW 8.1
- **C++ Standard:** C++17
- **Platform:** Windows 10/11
- **Python:** 3.13 (Miniconda, for future integration)

## Architecture

### Directory Structure

```
DrillControl/
├── src/                          # Source implementation
│   ├── main.cpp                  # Entry point
│   ├── Global.cpp                # Global handle & mutex
│   ├── ui/                       # UI layer (pages)
│   ├── dataACQ/                  # Data acquisition workers
│   ├── database/                 # Database writer & querier
│   └── control/                  # Motion control layer
├── include/                      # Headers (mirrors src/)
├── forms/                        # Qt UI files (.ui)
├── config/                       # Runtime configuration
│   └── mechanisms.json           # Mechanism parameters (hot-reload)
├── docs/                         # Documentation
└── thirdparty/                   # Third-party libraries
    ├── qcustomplot/              # Plotting library
    ├── vk701/                    # VK701 DAQ card
    ├── zmotion/                  # ZMotion motion controller
    └── sqlite3/                  # SQLite database
```

### Thread Architecture

| Thread | Purpose | Frequency | Thread-Safety |
|--------|---------|-----------|---------------|
| Main Thread | UI + Motion Control | - | Uses `g_mutex` for ZMotion API |
| VibrationThread | VK701 sensor acquisition | 5000Hz | Independent |
| MdbThread | Modbus sensor acquisition | 10Hz | Independent |
| MotorThread | Motor parameter polling | 100Hz | Uses `g_mutex` (read-only) |
| DbThread | Async database writes | Batch | Independent |

### Single Handle Architecture

**Critical Design:** All ZMotion API calls use a **single global handle** (`g_handle`) protected by `g_mutex`.

```cpp
// include/Global.h
extern ZMC_HANDLE g_handle;  // Single ZMotion handle
extern QMutex g_mutex;       // Protects all ZAux_* calls
extern int MotorMap[10];     // EtherCAT motor mapping
```

**Rule:** Every `ZAux_*` API call MUST hold `g_mutex` using `QMutexLocker`.

### Motion Interlock System

**File:** `include/control/MotionLockManager.h`

Prevents simultaneous motion operations from conflicting:

```cpp
enum class MotionSource {
    None,           // Idle
    ManualJog,      // Manual jogging
    ManualAbs,      // Manual absolute move
    AutoScript,     // Automatic scripted motion
    Homing          // Homing operation
};
```

**Key Methods:**
- `requestMotion()` - Request permission (shows conflict dialog if busy)
- `releaseMotion()` - Release permission after motion completes
- `emergencyStop()` - Unconditional stop all motors

**Conflict Handling:**
- Manual operations can interrupt automatic scripts (with confirmation)
- Emergency stop always takes priority
- Data acquisition threads use read-only access (no interlock needed)

### Mechanism Controller Architecture

**Base Class:** `BaseMechanismController` (`include/control/BaseMechanismController.h`)

All 9 mechanism controllers inherit from this base:

```cpp
class BaseMechanismController {
    // Pure virtual - must implement
    virtual bool initialize() = 0;
    virtual bool stop() = 0;
    virtual bool reset() = 0;
    virtual void updateStatus() = 0;

    // Motion interlock helpers
    bool requestMotionLock(const QString& description);
    void releaseMotionLock();
    bool hasMotionLock() const;
};
```

**9 Mechanism Controllers:**

| Code | Name | Class | Control Mode | Motor ID | Protocol |
|------|------|-------|--------------|----------|----------|
| Fz | Feed | FeedController | Position | 2 | EtherCAT |
| Pr | Rotation | RotationController | Velocity/Torque | 0 | EtherCAT |
| Pi | Percussion | PercussionController | Position/Velocity/Torque | 1 | EtherCAT |
| Cb | Clamp (Bottom) | ClampController | Torque | 3 | EtherCAT |
| Sr | Storage | StorageController | Position | 7 | EtherCAT |
| Dh | Docking | DockingController | Position | - | Modbus TCP |
| Me | Arm Extension | ArmExtensionController | Position | 6 | EtherCAT |
| Mg | Arm Grip | ArmGripController | Torque | 4 | EtherCAT |
| Mr | Arm Rotation | ArmRotationController | Position | 5 | EtherCAT |

**Initialization Pattern:**
- Most controllers use **stall detection** (position stabilization) for homing
- No external limit switches - all rely on motor encoders
- Exception: `DockingController` uses Modbus status registers

**Configuration:** All mechanism parameters in `config/mechanisms.json` (hot-reloadable via `MotionConfigManager`)

### Key Design Principles

1. **KISS Principle:** Simple, maintainable code without over-engineering
2. **No Defensive Programming:** Trust internal code paths, validate only at boundaries
3. **Minimal Abstractions:** Only create helpers when truly needed (3+ uses)
4. **Thread Safety via Single Handle:** One global ZMotion handle with mutex protection
5. **Motion Safety via Interlocks:** Prevent conflicting operations with user confirmation

## Important Code Patterns

### Accessing ZMotion API

```cpp
#include "Global.h"
#include <QMutexLocker>

void someMotionOperation() {
    QMutexLocker locker(&g_mutex);  // Always lock first
    if (!g_handle) {
        // Handle error
        return;
    }

    int result = ZAux_Direct_GetMpos(g_handle, motorId, &position);
    // Use result...
}
```

### Mechanism Motion Pattern

```cpp
bool MyController::doMotion(double target) {
    // 1. Request motion interlock
    if (!requestMotionLock("MyController: Moving to " + QString::number(target))) {
        return false;  // User cancelled or denied
    }

    // 2. Perform motion (with g_mutex inside driver calls)
    bool success = driver()->moveAbsolute(motorId, target);

    // 3. Release interlock when done
    releaseMotionLock();

    return success;
}
```

### Configuration Hot-Reload

```cpp
// MotionConfigManager is a singleton
auto config = MotionConfigManager::instance();

// Get mechanism config
MechanismConfig fzConfig = config->getMechanismConfig("Fz");
double velocity = fzConfig.velocity;

// Get key position
double positionA = config->getKeyPosition("Fz", "A");
```

## UI Pages

| Page | Purpose | Key Features |
|------|---------|--------------|
| SensorPage | Sensor acquisition control | Start/stop acquisition, display rates |
| VibrationPage | Vibration monitoring | Real-time waveform plotting |
| MdbPage | Modbus sensor display | Force, position, torque display |
| MotorPage | Motor parameter display | Motor status monitoring |
| ControlPage | Manual motor control | 10-axis motor table, jog control, command terminal |
| DatabasePage | Database management | Query interface, data export |
| DrillControlPage | Mechanism control | 9 mechanism controllers in 3x3 grid |

**ControlPage Features:**
- Editable motor parameter table (EN, MPos, Pos, MVel, Vel, DAC, Atype, Unit, Acc, Dec)
- Double-click to edit → triggers motion for position changes
- Absolute/Relative position mode toggle
- Clear alarm & set zero point
- ZMotion BASIC command terminal

## Configuration Files

### config/mechanisms.json

JSON structure for all mechanism parameters:

```json
{
  "mechanisms": {
    "Fz": {
      "name": "进给机构",
      "motor_id": 2,
      "velocity": 50.0,
      "acceleration": 100.0,
      "key_positions": {
        "A": 0,
        "H": 13100000
      }
    }
  }
}
```

Changes are detected via `QFileSystemWatcher` and automatically reloaded.

## Testing & Debugging

**No formal test suite yet.** Testing is manual via:
- ControlPage for individual motor control
- DrillControlPage for mechanism testing
- Command terminal for direct ZMotion BASIC commands

**Debugging Motion Issues:**
1. Check `MotionLockManager` for interlock conflicts
2. Verify `g_handle` is initialized (check main thread logs)
3. Use `?Map` command in ControlPage terminal to verify motor mapping
4. Check mechanism state via `DrillControlPage` status labels

## Common Pitfalls

1. **Forgetting `g_mutex`:** Every `ZAux_*` call needs mutex protection
2. **Not Releasing Motion Lock:** Always call `releaseMotionLock()` after motion
3. **Assuming Sensors Exist:** System has NO external limit switches - all is encoder-based
4. **Editing Without Reading:** ALWAYS read existing code before modifying
5. **Over-Engineering:** Keep solutions simple - don't add unnecessary abstractions

## Documentation

Key docs in `docs/`:
- `MOTION_INTERLOCK_SYSTEM.md` - Motion safety design
- `MECHANISM_CONTROLLERS_GUIDE.md` - Detailed controller specifications
- `CONTROL_PAGE_MIGRATION_REPORT.md` - ControlPage refactoring history
- `DRILLCONTROLPAGE_IMPL.md` - DrillControlPage implementation notes
- `KEY_POSITIONS_GUIDE.md` - Critical position definitions

## Code Style

- **Language:** Chinese comments for domain-specific terms, English for code
- **Naming:** camelCase for methods, PascalCase for classes
- **Headers:** Minimal comments - code should be self-explanatory
- **Error Handling:** User-facing error messages in Chinese
- **No Emojis:** Unless explicitly requested

## Version History

- **v2.0:** Complete rewrite with modular architecture
  - ControlPage refactored (6786 → 610 lines, -91%)
  - Added DrillControlPage with 9 mechanism controllers
  - Implemented motion interlock system
  - Single handle + mutex architecture
