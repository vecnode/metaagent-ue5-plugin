# MetaAgentPlugin

Under heavy development. 

- UE5 plugin with several runtimes for a multimodal agent with Humanoid structure.
- Modules: MetaAgentPlugin (Runtime), MetaAgentPluginEditor (Editor).

Objective: Allow an active agent to control the cinematic area. This plugin creates new levels and has default BPs to use. 

## Flowchart

```mermaid
flowchart TD
	A[MetaAgent]
	B[CharacterRuntime]
	C[CameraRuntime]
	D[GUIRuntime]
	E[NetworkingRuntime]
	F[RecordingRuntime]
	G[AIRuntime]
	H[ParticleRuntime]

	A --> B
	A --> C
	A --> D
	A --> E
	A --> F
	A --> G
	A --> H
```

<details>
<summary>Architecture</summary>

```mermaid
flowchart TB
    subgraph Settings
        PS[UMetaAgentPluginSettings]
        MA[AMetaAgentMainActor]
    end

    subgraph Global
        G[GMetaAgentRuntimeActive]
    end

    subgraph Game
        GM[AMetaAgentGameMode]
        PC[AMetaAgentPlayerController]
        HUD[AMetaAgentHUD]
        GI[UMetaAgentGameInstance]
    end

    subgraph Runtimes
        CR[FMetaAgentCameraRuntime]
        CHR[FMetaAgentCharacterRuntime]
        GR[FMetaAgentGUIRuntime]
        AR[AIRuntime partials]
        RR[RecordingRuntime partials]
        PR[UMetaAgentParticleRuntime]
    end

    PS --> G
    MA --> G
    G --> GM
    G --> PC
    G --> GI
    GM --> PC
    PC --> CR
    PC --> CHR
    PC --> GR
    PC --> AR
    PC --> RR
    PC --> PR
    PC --> HUD
    GR --> GI
    PR -->|Niagara callback| PC
```
</details>


## Modules

### Module 1 : MetaAgentCharacterRuntime

- Default pawn spawn and possession via game mode
- Blueprint-owned camera/mesh/animation setup
- Minimal runtime bootstrap (no recovery pipeline)
- Implemented in:
	- `Systems/CharacterRuntime/MetaAgentCharacterRuntime.h`
	- `Systems/CharacterRuntime/MetaAgentCharacterRuntime.cpp`

#### Character runtime graph

```mermaid
flowchart TB
    subgraph Config
        GM_CFG[AMetaAgentGameMode]
        PAWN_CFG[DefaultPlayerPawnClass]
        BP[BP_MH_PlayerChar]
    end

    subgraph Spawn
        RESTART[RestartPlayer]
        SPAWN[Spawn default pawn]
        POSSESS[Possess pawn]
    end

    subgraph Game
        PC[AMetaAgentPlayerController]
        PAWN[APawn / ACharacter]
    end

    subgraph CharacterRuntime
        CHR[FMetaAgentCharacterRuntime]
        BOOT[RunPossessedCharacterBootstrapSequence]
    end

    GM_CFG --> PAWN_CFG
    PAWN_CFG --> BP
    GM_CFG --> RESTART
    RESTART --> SPAWN
    SPAWN --> PAWN
    RESTART --> POSSESS
    POSSESS --> PC
    PC --> PAWN
    PC -->|OnPossess| CHR
    CHR --> BOOT
    BOOT --> PAWN
    BP -->|owns camera mesh animation| PAWN
```

<details>
<summary>Module 1: simplified runtime flow</summary>

1. Resolve `DefaultPlayerPawnClass` from `MetaAgentGameMode` config.
2. Spawn and possess the default pawn through standard `AGameModeBase::RestartPlayer`.
3. Keep CharacterRuntime bootstrap minimal (no mesh/anim recovery pipeline).
4. Let `BP_MH_PlayerChar` own camera, mesh, and animation setup directly.

</details>

### Module 2: MetaAgentCameraRuntime (Environment Viewer)

- Environment-only camera system for pure scene exploration
- Free-look with mouse and keyboard movement
- Mouse wheel zoom for distance control
- Cinematic orbital camera mode (`O`)
- Simplified for viewer-only without character dependencies
- Implemented in:
	- `Systems/CameraRuntime/MetaAgentCameraRuntime.h`
	- `Systems/CameraRuntime/MetaAgentCameraRuntime.cpp`

#### Camera runtime graph

```mermaid
flowchart TB
    subgraph Input
        O[O toggle cinematic]
        WHEEL[Mouse wheel zoom]
        LOOK[Mouse look fallback]
        MOVE[WASD movement fallback]
    end

    subgraph Game
        PC[AMetaAgentPlayerController]
        ZOOM[FMetaAgentCameraZoomState]
        CIN[FMetaAgentCinematicCameraState]
    end

    subgraph CameraRuntime
        BRIDGE[MetaAgentPlayerControllerCamera]
        CR[FMetaAgentCameraRuntime]
        ZOOM_SEQ[RunEnvironmentZoomSequence]
        TOGGLE[RunToggleCinematicCameraSequence]
        ENABLE[RunEnableCinematicCameraSequence]
        UPDATE[RunUpdateCinematicCameraSequence]
        DISABLE[RunDisableCinematicCameraSequence]
    end

    subgraph Modes
        FREE[FreeLook environment viewer]
        ORBIT[Cinematic orbital camera]
    end

    CAM[ACameraActor transient]
    TARGET[ResolveCinematicTargetActor]

    O --> PC
    WHEEL --> PC
    LOOK --> PC
    MOVE --> PC
    PC --> BRIDGE
    BRIDGE --> CR
    PC --> ZOOM
    PC --> CIN
    PC -->|PlayerTick| ZOOM_SEQ
    ZOOM_SEQ --> ZOOM
    ZOOM_SEQ --> FREE
    O --> TOGGLE
    TOGGLE -->|enable| ENABLE
    TOGGLE -->|disable| DISABLE
    ENABLE --> CAM
    ENABLE --> TARGET
    ENABLE --> ORBIT
    PC -->|PlayerTick while active| UPDATE
    UPDATE --> CAM
    UPDATE --> ORBIT
    DISABLE --> FREE
    DISABLE -->|restore view| PC
```

<details>
<summary>Module 2: Simplified runtime steps (environment viewing only)</summary>

1. Keep `AMetaAgentPlayerController` as the input owner for camera actions.
2. Route all camera execution through `FMetaAgentCameraRuntime` sequences.
3. Keep `FMetaAgentCameraZoomState` in the controller for zoom interpolation and bounds.
4. Consume discrete mouse-wheel up/down zoom input (step in/out).
5. Consume analog wheel-axis zoom input for smooth zooming.
6. Interpolate camera zoom distance toward desired distance.
7. Clamp zoom distances to configured min/max bounds (40–1500 units).
8. Clamp and sanitize zoom tuning values before runtime use.
9. Route `O` toggle handling through runtime cinematic-toggle sequence.
10. Enable cinematic mode through runtime camera-activation sequence (smooth orbital motion).
11. Disable cinematic mode through runtime camera-teardown sequence (restore free-look).
12. Update cinematic camera every frame through runtime update sequence.
13. Spawn a transient runtime `ACameraActor` when cinematic mode starts.
14. Reuse existing runtime cinematic camera actor when still valid.
15. Rebuild runtime cinematic camera actor when stale or world-mismatched.
16. Preserve pre-cinematic view state for deterministic restore on exit.
17. Restore view to free-look mode on cinematic exit.
18. Keep player input fully enabled during all camera modes (free exploration).
19. Apply oscillating-hold cinematic motion style (smooth sway around focus point).
20. Apply runtime sway and look-at behavior for cinematic framing.
21. Auto-disable cinematic mode when runtime camera prerequisites are lost.
22. Keep the runtime camera module extensible for additional orbital styles.


</details>



### Module 3: MetaAgentGUIRuntime

- Runtime GUI panel orchestration owned by a dedicated module
- HUD help panel visibility toggle bound to keyboard (`Q`)
- Keyboard-function reference panel rendered when GUI help is active
- Implemented in:
	- `Systems/GUIRuntime/MetaAgentGUIRuntime.h`
	- `Systems/GUIRuntime/MetaAgentGUIRuntime.cpp`
	- `Systems/GUIRuntime/MetaAgentPlayerControllerGUI.cpp`

#### GUI runtime graph

```mermaid
flowchart TB
    subgraph Input
        Q[Q toggle help panel]
    end

    subgraph Game
        PC[AMetaAgentPlayerController]
        GSTATE[FMetaAgentGUIState]
    end

    subgraph GUIRuntime
        BRIDGE[MetaAgentPlayerControllerGUI]
        GR[FMetaAgentGUIRuntime]
        TOGGLE[RunToggleHelpPanelSequence]
        APPLY[RunApplyHelpPanelSequence]
        INIT[InitializeDefaultHelpPanelLines]
        REBUILD[RebuildDisplayHelpPanelLines]
    end

    subgraph HUDPanels
        MAHUD[AMetaAgentHUD]
        HELP[Help panel canvas]
        REC[Recording panel lines]
        NET[Networking panel lines]
        STATUS[Status lines]
    end

    Q --> PC
    PC --> BRIDGE
    BRIDGE --> TOGGLE
    TOGGLE --> GR
    GR --> GSTATE
    TOGGLE --> APPLY
    APPLY --> INIT
    APPLY --> REBUILD
    REBUILD --> GSTATE
    APPLY --> MAHUD
    MAHUD --> HELP
    APPLY --> REC
    PC -->|BuildRecordingRuntimePanelLines| REC
    PC -->|GetNetworkingRuntimePanelLines| NET
    GSTATE -->|bHelpPanelVisible| HELP
    GSTATE -->|RecordingStatusLine| REBUILD
    GR --> STATUS
```

<details>
<summary>Module 3: 20 sequential runtime steps</summary>

1. Keep `AMetaAgentPlayerController` as input owner for GUI toggle actions.
2. Route GUI panel behavior through `FMetaAgentGUIRuntime` sequences.
3. Keep `FMetaAgentGUIState` in the controller as runtime GUI state storage.
4. Bind `Q` key in controller utility input setup for help panel toggling.
5. Handle `Q` key press through a dedicated controller-to-runtime bridge.
6. Initialize default keyboard-help lines on first GUI runtime application.
7. Keep help panel lines cached in runtime GUI state for deterministic redraw.
8. Apply GUI runtime state to HUD through explicit runtime apply sequence.
9. Push canonical help lines from GUI runtime to HUD each apply cycle.
10. Push help-panel visibility flag from GUI runtime to HUD each apply cycle.
11. Toggle help-panel visibility state in runtime sequence on each `Q` press.
12. Emit runtime log entries when help panel visibility changes.
13. Keep GUI toggle behavior free of transient keypress popup text.
14. Render help panel title and key-function rows via HUD canvas drawing.
15. Include active runtime key rows for recording (`J`/`U`) and COMMS (`H`/`G`).
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
- Bottom-left Networking Runtime GUI panel shown when GUI (`Q`) is active
- Implemented in:
	- `Systems/NetworkingRuntime/MetaAgentGameInstanceNetworking.cpp`
	- `Systems/NetworkingRuntime/MetaAgentGameInstance.h`
	- `Systems/NetworkingRuntime/MetaAgentGameInstance.cpp`

#### Networking runtime graph

```mermaid
flowchart TB
    subgraph Settings
        PS[UMetaAgentPluginSettings]
        PORT[LocalHttpServerPort]
        PLATFORM[PlatformBaseUrl + EventEndpoint]
    end

    subgraph GameInstance
        GI[UMetaAgentGameInstance]
        SNAP[NetworkingRuntime snapshot]
    end

    subgraph LocalServer
        HTTP[Embedded HTTP server]
        ROUTER[IHttpRouter]
        HEALTH["GET /health"]
        ECHO["POST /echo"]
        NOTIFY["POST /notify"]
    end

    subgraph Outbound
        PC[AMetaAgentPlayerController]
        H[H start audio]
        G[G start image]
        FWD[SendPlatformEvent]
    end

    subgraph GUI
        GR[FMetaAgentGUIRuntime]
        HUD[AMetaAgentHUD networking panel]
    end

    EXT[(External platform API)]

    PS --> GI
    PORT --> GI
    PLATFORM --> GI
    GI -->|Init| HTTP
    HTTP --> ROUTER
    ROUTER --> HEALTH
    ROUTER --> ECHO
    ROUTER --> NOTIFY
    NOTIFY --> SNAP
    HEALTH --> SNAP
    H --> PC
    G --> PC
    PC --> FWD
    FWD --> EXT
    EXT -->|response| SNAP
    GI -->|Shutdown| HTTP
    SNAP --> GR
    GR --> HUD
```

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

- Runtime recording based on Unreal Engine Movie Scene Capture (FrameGrabber, no HiResShot loop)
- `J` toggles viewport capture start/stop and writes an AVI video file directly to disk
- Video is captured from the active player viewport at a fixed FPS (default 30)
- Output directory is created under `Saved/Renders/Capture_YYYYMMDD_HHMMSS`
- `U` finalizes capture (if active) and reports output/status summary
- Recording panel shows runtime capture state, resolution, frame count, and output path
- Video compression can be toggled on the player controller (`bUseVideoCompression`, `VideoCompressionQuality`)
- Implemented in:
	- `Systems/RecordingRuntime/MetaAgentPlayerControllerRecording.cpp`

#### Recording runtime graph

```mermaid
flowchart TB
    subgraph Input
        J[J toggle capture]
        U[U finalize / status]
    end

    subgraph Game
        PC[AMetaAgentPlayerController]
        RS[FMetaAgentRecordingState]
    end

    subgraph RecordingRuntime
        RR[RecordingRuntime partials]
        START[StartViewportRecording]
        STOP[StopViewportRecording]
        REPORT[ReportRuntimeCaptureStatus]
        METRICS[UpdateRecordingCaptureStatus]
    end

    subgraph MovieSceneCapture
        MSC[UMovieSceneCapture]
        RTS[FRealTimeCaptureStrategy]
        FG[FrameGrabber]
        VIDEO[UVideoCaptureProtocol AVI]
    end

    VP[FSceneViewport]
    OUT[(Saved/Renders/Capture_*)]
    GUI[FMetaAgentGUIRuntime]
    HUD[AMetaAgentHUD recording panel]

    J --> PC
    U --> PC
    PC --> RR
    RR --> START
    RR --> STOP
    RR --> REPORT
    PC --> RS
    START --> MSC
    STOP --> MSC
    REPORT --> STOP
    MSC --> RTS
    MSC --> FG
    MSC --> VIDEO
    VP --> FG
    FG --> VIDEO
    VIDEO --> OUT
    PC -->|PlayerTick while active| METRICS
    METRICS --> RS
    START --> HUD
    STOP --> HUD
    REPORT --> HUD
    RR --> GUI
    GUI --> HUD
    PC -->|EndPlay| STOP
```

<details>
<summary>Module 5: 12 sequential runtime steps</summary>

1. Press `J` to start viewport video capture.
2. Initialize a new output folder in `Saved/Renders`.
3. Configure Movie Scene Capture with `UVideoCaptureProtocol` (AVI output).
4. Bind capture to the local game viewport and start recording.
5. Capture at configured fixed FPS (default 30 FPS).
6. Engine FrameGrabber streams frames into the AVI writer while gameplay continues.
7. Update recording runtime panel line values continuously.
8. Press `J` again to stop capture and finalize the AVI file.
9. Keep the captured video in the output directory.
10. Press `U` to finalize/status summary for the current or last capture session.
11. Use the output AVI directly or transcode externally if needed.
12. Capture resolution defaults to viewport size; optional override via recording settings.

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

#### AI runtime graph

```mermaid
flowchart TB
    subgraph Input
        I[I toggle autopilot]
    end

    subgraph Game
        PC[AMetaAgentPlayerController]
        AP[FMetaAgentAutopilotState]
        PAWN[Possessed pawn]
    end

    subgraph AIRuntime
        BRIDGE[MetaAgentPlayerControllerAutopilot]
        ENABLE[EnableAutopilotForCurrentPawn]
        DISABLE[DisableAutopilotAndRepossess]
        DEBOUNCE[Toggle debounce guard]
    end

    subgraph AIController
        AIC[AMetaAgentWanderAIController]
        BT[Runtime behavior tree]
    end

    subgraph PatrolLoop
        PICK[Pick random patrol point]
        MOVE[MoveTo patrol location]
        WAIT[Wait random interval]
    end

    I --> PC
    PC --> BRIDGE
    BRIDGE --> DEBOUNCE
    DEBOUNCE -->|off| ENABLE
    DEBOUNCE -->|on| DISABLE
    ENABLE --> AP
    ENABLE --> AIC
    PC -->|unpossess| PAWN
    AIC -->|possess| PAWN
    AIC --> BT
    BT --> PICK
    PICK --> MOVE
    MOVE --> WAIT
    WAIT --> PICK
    DISABLE -->|destroy AI| AIC
    DISABLE -->|repossess| PC
    DISABLE --> AP
```

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


### Module 7: MetaAgentParticleRuntime

- Tracks Niagara components in the active world (default name filter: `NIAGARA`).
- Captures ~1k particle world positions via direct C++ GPU readback (`FScopedNiagaraDataSetGPUReadback`) and CPU dataset access.
- `V` plays particle choreography: **Forming → Holding → Returning → Idle**.
- **Shape builder** (`FMetaAgentParticleShapeBuilder`) resolves target positions from configurable shapes (default: **ImageSilhouette** with **Sobel edge** sampling; fallback: **SquareGrid**).
- `F` loads `sdxl_latest.png` onto the preview plane and provides texture + transform for image shapes.
- Pattern actuation writes blended positions back into Niagara simulation buffers (`PushCPUBuffersToGPU` on GPU emitters).
- Help panel (`Q`) shows capture status, preset/timings, active shape, and live pattern state.
- Timings: **MetaAgent | Particles | Pattern** (`FMetaAgentParticlePatternConfig`).
- Shape: **MetaAgent | Particles | Pattern | Shape** (`FMetaAgentParticleShapeDefinition`).
- `B` = Slow preset, `N` = Dramatic preset. Press `V` after choosing timings.
- Console (PIE): `MetaAgent.Pattern.Form`, `.Hold`, `.Return`, `.Preset`, `.Status`, `.Shape`, `.ImageSampling Sobel|Fill`, `.EdgeThreshold`, `.ImageThreshold`, `.ShapeWidth`.
- Implemented in:
	- `Systems/ParticleRuntime/MetaAgentParticleRuntime.h/.cpp`
	- `Systems/ParticleRuntime/MetaAgentParticlePatternTypes.h/.cpp`
	- `Systems/ParticleRuntime/MetaAgentParticleShapeTypes.h`
	- `Systems/ParticleRuntime/MetaAgentParticleShapeBuilder.h/.cpp`
	- `Systems/ParticleRuntime/MetaAgentImagePreviewRuntime.h/.cpp`
	- `Systems/ParticleRuntime/MetaAgentPlayerControllerParticles.cpp`

#### Pattern architecture

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Forming: V (snapshot + shape targets)
    Forming --> Holding: elapsed >= FormDuration, phase 0→1
    Holding --> Returning: elapsed >= HoldDuration, phase = 1
    Returning --> Idle: elapsed >= ReturnDuration, phase 1→0

```

#### Shape pipeline

```mermaid
flowchart LR
    F[F key PNG] --> TEX[LatestPngPreviewTexture]
    F --> PLANE[Preview plane transform]
    V[V key] --> SNAP[Snapshot baselines]
    SNAP --> BUILD[FMetaAgentParticleShapeBuilder]
    TEX --> BUILD
    PLANE --> BUILD
    BUILD --> IMG[ImageSilhouette]
    BUILD --> GRID[SquareGrid fallback]
    IMG --> NN[Nearest-neighbor assign]
    NN --> TGT[PatternWorldTargets]
    GRID --> TGT
    TGT --> FSM[Forming / Holding / Returning]
```

<details>
<summary>Module 7: sequential runtime steps</summary>

1. Runtime discovers tracked Niagara components (for example `NIAGARAFX` on `BP_NiagaraBridge`).
2. Direct capture reads particle `Position` attributes into `UMetaAgentParticleRuntime` snapshot cache.
3. Help panel reports `Particle Capture: TRUE (Count=N)` once data is available.
4. Player presses `F` to load `sdxl_latest.png` (preview plane + shape texture).
5. Player optionally presses `B` / `N` for timing presets.
6. Player presses `V` to start the pattern.
7. Runtime snapshots baselines and freezes timings + shape config for the run.
8. `FMetaAgentParticleShapeBuilder` builds `PatternWorldTargets` (image silhouette with nearest-neighbor assignment, or square grid fallback).
9. State machine enters `Forming` and lerps baseline → targets (phase 0→1).
10. GPU emitters: readback → modify CPU float buffer → `PushCPUBuffersToGPU`.
11. `Holding` locks phase at 1 on the resolved shape.
12. `Returning` drives phase 1→0 back to baseline.
13. On completion, state returns to `Idle` and normal capture resumes.
14. Blueprint API: `StartParticleSquarePattern()`, `SetParticlePatternShape()`, `ApplyParticlePatternPreset()`, `SetParticlePatternTimings()`.

</details>

## License

Licensed under the [MIT License](./LICENSE).