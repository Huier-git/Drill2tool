# Acquisition/Auto-Task Critical Fixes (2025-12-21)

## Background
This update addresses high-risk issues in the acquisition pipeline and its integration with the auto-task module, focusing on sampling threads, data wiring, time-window alignment, and round-scoped caching.

## Issues Found
1. Sampling timers never fired: MDB/Motor workers entered blocking loops after starting QTimer, preventing timeout delivery and effectively stalling sampling.
2. Modbus connect/disconnect could hang: connection state was polled with sleep only, without an event loop, so ConnectingState could stall indefinitely.
3. Auto-task not wired to acquisition data: when controllers were set after the acquisition manager, data workers were never reconnected, so auto-task saw no sensor data.
4. Time-window cache cross-contamination: DbWriter cached time_windows by window_start_us only; if a new round starts within the same second, data could be written into the previous round's window.

## Fixes Applied
- MDB/Motor workers: removed blocking loops, start timer and return to allow event loop delivery; statistics now emitted from read callbacks.
  - Files: `src/dataACQ/MdbWorker.cpp`, `src/dataACQ/MotorWorker.cpp`
- Modbus connect/disconnect: added a local event loop with timeout to avoid hanging in ConnectingState.
  - File: `src/dataACQ/MdbWorker.cpp`
- Auto-task data wiring: ensure data workers are connected after controllers are set; add sensor connectivity/freshness checks.
  - Files: `src/ui/AutoTaskPage.cpp`, `src/control/AutoDrillManager.cpp`, `include/control/AutoDrillManager.h`
- Time-window cache: key changed to (round_id, window_start_us), and cache is cleared on new round creation.
  - Files: `src/database/DbWriter.cpp`, `include/database/DbWriter.h`

## Behavior Changes
- MDB/Motor sampling no longer stalls due to blocked event loops.
- Modbus connection failures no longer hang indefinitely.
- Auto-task now verifies that sensor data is connected or recently received.
- time_windows are correctly scoped per round; no cross-round window reuse.

## Impact
- Acquisition reliability improved for MDB/Motor sampling.
- Auto-task safety checks are more accurate and resilient.
- Time alignment and round isolation in storage are correct.

## Verification Suggestions
- With hardware connected:
  1) Start MDB/Motor sampling and confirm continuous writes.
  2) Start an auto task and confirm prompts align with sensor connectivity.
  3) Create two rounds back-to-back and verify time_windows are assigned to the correct round.

## Related Commit
- `6ecf535` Fix acquisition timing and auto-task data wiring
