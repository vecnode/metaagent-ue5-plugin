# MetaAgentPlugin

Under heavy development. 

- UE5 plugin focused on possessing a placed MetaHuman and enabling movement at runtime.
- Modules: MetaAgentPlugin (Runtime), MetaAgentPluginEditor (Editor).

## What Works Now

### Module 1 : MetaAgentCharacterRuntime

- Character resolution and possession
- Mesh and animation readiness
- Deterministic fallback locomotion bootstrap
- Implemented in:
	- `Systems/CharacterRuntime/MetaAgentCharacterRuntime.h`
	- `Systems/CharacterRuntime/MetaAgentCharacterRuntime.cpp`

<details>
<summary>Module 1: 52 sequential runtime steps</summary>

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
51. Preload fallback locomotion assets
52. Activate immediate crowd fallback locomotion

</details>

### Module 2: MetaAgentCameraRuntime

- Camera bootstrap and attachment ownership
- Standard playable camera modes (`P`)
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
31. Execute `P` camera mode cycling entirely through runtime mode-cycle sequence.
32. Block `P` mode cycling while cinematic mode is active.
33. Display a HUD transient warning when `P` is pressed during cinematic mode.
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

Lorem ipsum



## Minimum Setup

- Startup GameMode: AMetaAgentGameMode (or derived BP).
- Placed actor name: MAIN_CHARACTER.
- Keep strict possession settings:
	- bRequireExactPreferredPawnName=True
	- bRequireUniquePreferredPawnName=True
	- bAllowSpawnFallback=False


## License

Licensed under the [MIT License](./LICENSE).