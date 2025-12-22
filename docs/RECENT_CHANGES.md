# Recent Changes

Scope: local commits ahead of origin/main.

## Commits
- 55162c6 Handle parallel ops in plan duration estimation.
  - Allow multiple mappings per step and take the slowest move for duration.
  - Add spin entries to `config/plan_step_map.csv` for parallel ops in serial plan.
  - Remove legacy auto task sample configs under `config/auto_tasks`.
- 37bfb52 Add physical unit toggle and plan duration estimation.
  - Add ControlPage physical-unit display + conversion for table edits and status line.
  - Add UnitConverter with `config/unit_conversions.csv` overrides for pulses-per-unit.
  - Add PlanVisualizer auto duration estimation with `config/plan_step_map.csv` mapping.
  - Track SensorPage round reset connection state fields.
  - Update README recent updates.
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
