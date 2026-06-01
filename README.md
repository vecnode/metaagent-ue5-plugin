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
	- `Systems/Camera/MetaAgentCameraRuntime.h`
	- `Systems/Camera/MetaAgentCameraRuntime.cpp`

<details>
<summary>Module 2: 52 sequential runtime steps</summary>

1. Create `Systems/Camera/MetaAgentCameraRuntime.h`
2. Create `Systems/Camera/MetaAgentCameraRuntime.cpp`
3. Keep current camera behavior as the baseline during the refactor
4. Route controller camera calls through a dedicated runtime module
5. Preserve all working `P` camera modes exactly as they behave now
6. Preserve the working `O` cinematic oscillation behavior exactly as it behaves now
7. Keep the controller as the input owner, not the camera logic owner
8. Move camera-only helper functions into the runtime module first
9. Move first-person socket resolution into the runtime module
10. Move cinematic focus-location resolution into the runtime module
11. Move runtime spring-arm discovery into the runtime module
12. Move follow-camera discovery into the runtime module
13. Move runtime fallback creation of `CameraBoom` into the runtime module
14. Move runtime fallback creation of `FollowCamera` into the runtime module
15. Add a sequential camera bootstrap entry point for possessed pawns
16. Add a sequential standard camera application entry point
17. Add a sequential third-person zoom update entry point
18. Add a sequential cinematic camera toggle entry point
19. Add a sequential cinematic camera tick entry point
20. Keep `FMetaAgentCameraModeState` in the controller initially for low-risk migration
21. Keep `FMetaAgentCameraZoomState` in the controller initially for low-risk migration
22. Keep `FMetaAgentCinematicCameraState` in the controller initially for low-risk migration
23. Move `ApplyCameraModeToPawn` behavior behind the runtime module API
24. Move `ConfigureCameraForPawn` behavior behind the runtime module API
25. Move third-person camera setup into an explicit sequential step group
26. Move close over-shoulder camera setup into an explicit sequential step group
27. Move side cinematic close camera setup into an explicit sequential step group
28. Move first-person camera setup into an explicit sequential step group
29. Keep third-person zoom restricted to the third-person mode only
30. Preserve existing mouse-wheel interpolation behavior
31. Preserve the remembered third-person arm length across mode changes
32. Preserve existing runtime attachment repair when camera parent changes
33. Preserve existing first-person head-socket preference behavior
34. Preserve existing camera fallback logging behavior
35. Move `P` mode cycling logic to a runtime sequence owned by Module 2
36. Keep `P` key binding in the controller and call the runtime module from there
37. Move `O` toggle logic to a runtime sequence owned by Module 2
38. Keep `O` key binding in the controller and call the runtime module from there
39. Move cinematic target resolution into the runtime module
40. Move cinematic camera activation into the runtime module
41. Move cinematic camera deactivation into the runtime module
42. Move cinematic per-frame update into the runtime module
43. Preserve current non-blocking player-input behavior during cinematic mode
44. Preserve current oscillating-hold timing and amplitude behavior
45. Preserve current cinematic camera actor reuse behavior
46. Preserve current cinematic restore-view-target behavior
47. Reduce `AMetaAgentPlayerController` to orchestration-only camera calls
48. Validate the camera module with a focused build after each refactor phase
49. Verify `P` mode cycling after the runtime module is wired
50. Verify `O` cinematic toggle and oscillation after the runtime module is wired
51. Update the README to mark Module 2 as implemented only after the port is complete
52. Keep Module 2 extensible for future cinematic styles beyond the first oscillating type

</details>

## Minimum Setup

- Startup GameMode: AMetaAgentGameMode (or derived BP).
- Placed actor name: MAIN_CHARACTER.
- Keep strict possession settings:
	- bRequireExactPreferredPawnName=True
	- bRequireUniquePreferredPawnName=True
	- bAllowSpawnFallback=False

## Features

Module 1:
- Possess a placed MetaHuman at Play start through `AMetaAgentGameMode`.
- Run the full startup flow through the sequential `MetaAgentCharacterRuntime` module.
- Generate the camera at runtime so the plugin works even when the MetaHuman blueprint has no authored camera.
- Keyboard movement through fallback input, including walk and sprint.

Module 2:
- Run camera bootstrap and runtime attachment repair through `MetaAgentCameraRuntime`.
- Route all `P` camera mode cycling through the sequential camera runtime module.
- Route all `O` cinematic camera activation, update, and teardown through the sequential camera runtime module.
- Preserve third-person zoom continuity and the current oscillating cinematic behavior while keeping the controller as orchestration only.



## License

Licensed under the [MIT License](./LICENSE).