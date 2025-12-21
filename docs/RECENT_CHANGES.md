# Recent Changes

Scope: local commits ahead of origin/main.

## Commits
- fe74c6d Remove legacy AutoTask test scaffolding.
  - Remove test_mode build block, test UI, MockDataGenerator, and AutoTask unit test files.
- 868f36d Silence unused AutoTask state parameter.
  - Add Q_UNUSED for state in AutoTaskPage::onTaskStateChanged.
- 2f8eebe Align acquisition timestamps and avoid UI blocking.
  - Add BaseWorker time base + monotonic timestamp helper and set base per round.
  - Use aligned timestamps in Mdb/Motor/Vibration workers.
  - Remove UI thread sleep in ControlPage.
- 45837b0 Guard ZMotion handle access and VK701 stop responsiveness.
  - Add g_mutex guards around ZAux calls in UI.
  - Allow queued stop in VibrationWorker loop; avoid repeated StartSampling.
- 02ede2e Harden JSON config loading and saving.
  - Use atomic saves and validate JSON inputs for config and task files.
- 1a5a860 Document acquisition/auto-task fixes.
  - Update README and add docs/BUGFIX_20251221_Acquisition_AutoTask.md.
- 6ecf535 Fix acquisition timing and auto-task data wiring.

## Notes
- These commits are local (ahead of origin/main).
