# MetaAgentPlugin

Under heavy development. 

- UE5 plugin with several runtimes for humanoid agents.
- Modules: MetaAgentPlugin (Runtime), MetaAgentPluginEditor (Editor).

## Modules

### Module 1 : MetaAgentCharacterRuntime

- Character resolution and possession
- Mesh and animation readiness
- Deferred animation-class repair after bootstrap
- Implemented in:
	- `Systems/CharacterRuntime/MetaAgentCharacterRuntime.h`
	- `Systems/CharacterRuntime/MetaAgentCharacterRuntime.cpp`

<details>
<summary>Module 1: 53 sequential runtime steps</summary>

1. Validate controller and world
2. Capture existing pawn
3. Keep existing non-spectator pawn
4. Load preferred placed pawn class
5. Initialize selection slots
6. Scan placed pawns
7. Resolve strict name mode
8. Resolve unique named match
9. Handle ambiguous named match
10. Find matching non-pawn actor
11. Log missing strict-name actor mismatch
12. Log missing strict-name pawn
13. Decide spawn fallback for missing strict name
14. Possess selected placed pawn
15. Log selected placed pawn
16. Decide spawn fallback for no selection
17. Log no-selection error
18. Finalize possession sequence
19. Validate character
20. Validate primary mesh
21. Determine recovery need
22. Search attached recovery mesh
23. Search world recovery mesh
24. Apply recovered skeletal mesh
25. Apply recovered anim class directly
26. Apply recovered transform and visibility
27. Log recovery source
28. Apply runtime orientation offset
29. Try recovery owner anim class
30. Try recovery owner anim instance class
31. Try recovery owner CDO matching mesh
32. Try possessed character CDO anim class
33. Try world skeleton-matched anim class
34. Try plugin-local fallback anim classes
35. Initialize resolved anim blueprint
36. Log anim resolution status
37. Gather source meshes for follower recovery
38. Duplicate missing follower components
39. Record recovered scene map
40. Restore follower attachments
41. Restore follower relative transforms
42. Rebuild follower leader-pose links
43. Gather skeletal meshes
44. Resolve driving body mesh
45. Force driving mesh tick settings
46. Force no collision on meshes
47. Disable mesh overlap events
48. Rebind mesh leader-pose followers
49. Clamp movement speed guard
50. Clamp movement acceleration guard
51. Schedule deferred anim-class repair
52. Re-scan recovery owner/world for non-fallback body AnimBP on next tick
53. Replace temporary fallback AnimBP and reinitialize primary mesh anim instance when valid

</details>

### Module 2: MetaAgentCameraRuntime

- Camera bootstrap and attachment ownership
- Standard playable camera modes (`L`)
- Runtime cinematic camera orchestration (`O`)
- Third-person zoom and camera-state continuity
- Implemented in:
	- `Systems/CameraRuntime/MetaAgentCameraRuntime.h`
	- `Systems/CameraRuntime/MetaAgentCameraRuntime.cpp`

<details>
<summary>Module 2: 52 sequential runtime steps</summary>

1. Keep `AMetaAgentPlayerController` as the input owner for camera actions.
2. Route all camera execution through `FMetaAgentCameraRuntime` sequences.
3. Keep `FMetaAgentCameraModeState` in the controller as runtime-owned state data.
4. Keep `FMetaAgentCameraZoomState` in the controller as runtime-owned state data.
5. Keep `FMetaAgentCinematicCameraState` in the controller as runtime-owned state data.
6. Resolve first-person preferred sockets through runtime mesh-socket probing.
7. Resolve cinematic focus points through runtime head/chest socket selection.
8. Resolve cinematic target actors from possessed pawn, autopilot pawn, or view target.
9. Discover `CameraBoom` spring arm components by explicit name first.
10. Discover `FollowCamera` components by explicit name first.
11. Fall back to class-based spring-arm discovery when named lookup misses.
12. Fall back to class-based camera discovery when named lookup misses.
13. Create runtime `CameraBoom` fallback when a pawn has no spring arm.
14. Create runtime `FollowCamera` fallback when a pawn has no camera component.
15. Repair spring-arm parent attachment when the desired parent changes.
16. Repair spring-arm socket attachment when first-person socket selection changes.
17. Repair follow-camera parent attachment to the spring arm when needed.
18. Repair follow-camera parent attachment to camera parent when no spring arm exists.
19. Apply third-person camera offsets and arm length through a deterministic sequence.
20. Apply close over-shoulder offsets and arm length through a deterministic sequence.
21. Apply side cinematic close offsets and arm length through a deterministic sequence.
22. Apply first-person offsets and socket anchoring through a deterministic sequence.
23. Preserve and reuse remembered third-person arm length across mode changes.
24. Keep third-person zoom active only while third-person mode is active.
25. Consume discrete mouse-wheel up/down zoom input when present.
26. Consume analog wheel-axis zoom input when discrete wheel input is absent.
27. Interpolate spring-arm length toward desired third-person zoom distance.
28. Clamp zoom distances to configured min/max bounds before applying.
29. Clamp and sanitize zoom tuning values before runtime use.
30. Clamp and sanitize cinematic tuning values before runtime use.
31. Execute `L` camera mode cycling entirely through runtime mode-cycle sequence.
32. Block `L` mode cycling while cinematic mode is active.
33. Display a HUD transient warning when `L` is pressed during cinematic mode.
34. Route `ApplyCameraModeToPawn` through runtime camera-application sequence.
35. Route `ConfigureCameraForPawn` through runtime camera-application sequence.
36. Route third-person wheel zoom updates through runtime zoom sequence.
37. Route `O` toggle handling through runtime cinematic-toggle sequence.
38. Enable cinematic mode through runtime camera-activation sequence.
39. Disable cinematic mode through runtime camera-teardown sequence.
40. Update cinematic camera every frame through runtime update sequence.
41. Spawn a transient runtime `ACameraActor` when cinematic mode starts.
42. Reuse existing runtime cinematic camera actor when still valid.
43. Rebuild runtime cinematic camera actor when stale or world-mismatched.
44. Preserve pre-cinematic view target for deterministic restore on exit.
45. Restore view target to pre-cinematic target, pawn, or autopilot pawn on exit.
46. Keep player input enabled by default during cinematic mode.
47. Optionally disable move/look input during cinematic mode when configured.
48. Keep oscillating-hold cinematic motion style as the active implemented style.
49. Apply runtime sway and look-at behavior while tracking target focus continuity.
50. Auto-disable cinematic mode when required runtime camera prerequisites are lost.
51. Auto-disable cinematic mode when no valid cinematic target can be resolved.
52. Keep the runtime camera module extensible for additional cinematic styles.

</details>


### Module 3: MetaAgentGUIRuntime

- Runtime GUI panel orchestration owned by a dedicated module
- HUD help panel visibility toggle bound to keyboard (`H`)
- Keyboard-function reference panel rendered when GUI help is active
- Implemented in:
	- `Systems/GUIRuntime/MetaAgentGUIRuntime.h`
	- `Systems/GUIRuntime/MetaAgentGUIRuntime.cpp`
	- `Systems/GUIRuntime/MetaAgentPlayerControllerGUI.cpp`

<details>
<summary>Module 3: 20 sequential runtime steps</summary>

1. Keep `AMetaAgentPlayerController` as input owner for GUI toggle actions.
2. Route GUI panel behavior through `FMetaAgentGUIRuntime` sequences.
3. Keep `FMetaAgentGUIState` in the controller as runtime GUI state storage.
4. Bind `H` key in controller utility input setup for help panel toggling.
5. Handle `H` key press through a dedicated controller-to-runtime bridge.
6. Initialize default keyboard-help lines on first GUI runtime application.
7. Keep help panel lines cached in runtime GUI state for deterministic redraw.
8. Apply GUI runtime state to HUD through explicit runtime apply sequence.
9. Push canonical help lines from GUI runtime to HUD each apply cycle.
10. Push help-panel visibility flag from GUI runtime to HUD each apply cycle.
11. Toggle help-panel visibility state in runtime sequence on each `H` press.
12. Emit runtime log entries when help panel visibility changes.
13. Keep GUI toggle behavior free of transient keypress popup text.
14. Render help panel title and key-function rows via HUD canvas drawing.
15. Include `J`, `U`, and `Y` rows as reserved "not in flight yet" references in panel text.
16. Include fallback movement and look controls (`W/A/S/D`, `Shift`, mouse, wheel).
17. Keep help panel render path independent from status panel availability.
18. Keep recording status integrated into the main help panel with a separator row.
19. Preserve existing camera, autopilot, and recording runtime behavior unchanged.
20. Keep GUI runtime extensible for future panel types beyond controls help.

</details>

### Module 4: MetaAgentNetworkingRuntime

- Runtime networking orchestration through `UMetaAgentGameInstance`
- Embedded HTTP server for editor, standalone, and packaged runtime builds
- Runtime outbound platform event forwarding and inbound notify handling
- Bottom-left Networking Runtime GUI panel shown when GUI (`H`) is active
- Implemented in:
	- `Systems/NetworkingRuntime/MetaAgentGameInstanceNetworking.cpp`
	- `Systems/NetworkingRuntime/MetaAgentGameInstance.h`
	- `Systems/NetworkingRuntime/MetaAgentGameInstance.cpp`

<details>
<summary>Module 4: 20 sequential runtime steps</summary>

1. Keep `UMetaAgentGameInstance` as the runtime networking owner.
2. Initialize NetworkingRuntime snapshot state during game instance startup.
3. Start local HTTP server during runtime init when networking is enabled.
4. Embed server behavior in editor, standalone, and packaged runtime builds.
5. Bind `/health` endpoint for runtime health checks.
6. Bind `/echo` endpoint for payload round-trip checks.
7. Bind `/notify` endpoint for external notifications.
8. Start listeners after routes are bound.
9. Track router/listener state in runtime snapshot fields.
10. Expose runtime server status through `GetLocalHttpServerStatusText`.
11. Build platform forwarding URL from configured base and endpoint.
12. Send outbound platform events with runtime metadata payload.
13. Track last event name, send time, receive time, and result status.
14. Track HTTP/network errors in runtime snapshot fields.
15. Parse platform response payload for agent action/running state.
16. Track latest notify message from `/notify` requests.
17. Stop server and unbind routes during game instance shutdown.
18. Expose formatted NetworkingRuntime panel lines to GUI runtime.
19. Draw bottom-left networking rectangle only while GUI panel is active.
20. Keep networking runtime extensible for future endpoint families.

</details>


### Module 5: MetaAgentRecordingRuntime

- Simple runtime recording based on direct HiRes frame capture (no Take Recorder dependency)
- `J` toggles capture start/stop and writes PNG frames directly to disk
- Frames are captured from the active player camera at fixed FPS
- Output directory is created under `Saved/Renders/HiResFrames_YYYYMMDD_HHMMSS`
- `U` reports output/capture status (frames are already saved on disk)
- Recording panel shows runtime capture state, resolution, frame count, and output path

<details>
<summary>Module 5: 12 sequential runtime steps</summary>

1. Press `J` to start frame capture.
2. Initialize a new output folder in `Saved/Renders`.
3. Mark runtime recording active and reset counters.
4. Tick capture accumulator every frame.
5. Capture at configured fixed FPS (default 15 FPS).
6. For each capture step, request a HiRes screenshot using configured resolution.
7. Save frames as sequential PNG files (`frame_000000.png`, ...).
8. Update recording runtime panel line values continuously.
9. Press `J` again to stop capture.
10. Keep all captured frames in the output directory.
11. Press `U` to report save/status summary for the last capture session.
12. Use output frames directly for post-processing or external encoding.

</details>


### Module 6: MetaAgentAIRuntime

- Runtime AI wander controller (`AMetaAgentWanderAIController`) builds and runs a behavior tree in code.
- Autopilot toggle logic (`AMetaAgentPlayerController`) now lives in AIRuntime and hands possession to AI.
- Autopilot runtime toggle key is `I`.
- AI behavior is simple patrol wandering:
	- pick random patrol point in radius
	- move to patrol point
	- wait for random interval
	- repeat
- Implemented in:
	- `Systems/AIRuntime/MetaAgentWanderAIController.cpp`
	- `Systems/AIRuntime/MetaAgentPlayerControllerAutopilot.cpp`

<details>
<summary>Module 6: 10 sequential runtime steps</summary>

1. Player presses `I` to toggle autopilot.
2. Debounce guards prevent rapid toggle spam.
3. Controller caches currently possessed pawn.
4. Controller spawns runtime AI controller (`AMetaAgentWanderAIController` by default).
5. Player controller unpossesses pawn.
6. AI controller possesses pawn and starts runtime behavior tree.
7. Runtime behavior tree sets a random patrol location.
8. AI moves to location, then waits random interval.
9. Loop repeats for continuous roaming.
10. Press `I` again to unpossess AI, destroy it, and restore player possession.

</details>



## License

Licensed under the [MIT License](./LICENSE).