# MetaAgentPlugin

Under heavy development. 

- UE5 plugin focused on possessing a placed MetaHuman and enabling movement at runtime.
- Modules: MetaAgentPlugin (Runtime), MetaAgentPluginEditor (Editor).

## What Works Now

1. Create your MetaHuman in MetaHuman Creator.
2. Add that MetaHuman Blueprint to your map.
3. Name the placed actor MAIN_CHARACTER.
4. Use AMetaAgentGameMode (or a BP derived from it) as your startup GameMode.
5. At Play start, the plugin controller possesses MAIN_CHARACTER.
6. The plugin generates the camera at runtime, so MetaHumans work even when the blueprint has no authored camera.
7. Press `P` to cycle 4 camera positions: third-person, close over-shoulder, side-cinematic close, and first-person.
8. Keyboard movement works through fallback input (walk + sprint).

## Minimum Setup

- Startup GameMode: AMetaAgentGameMode (or derived BP).
- Placed actor name: MAIN_CHARACTER.
- Keep strict possession settings:
	- bRequireExactPreferredPawnName=True
	- bRequireUniquePreferredPawnName=True
	- bAllowSpawnFallback=False

## Features

- Possess a placed MetaHuman at Play start through `AMetaAgentGameMode`.
- Generate the camera at runtime so the plugin works even when the MetaHuman blueprint has no authored camera.
- Press `P` to cycle 4 camera positions: third-person, close over-shoulder, side-cinematic close, and first-person.
- Keyboard movement through fallback input, including walk and sprint.
- Plugin runtime and editor modules split for cleaner maintenance.

## Features Not Used Yet

- AI wander and patrol helpers.
- Autopilot control flow.
- Recording hooks.
- Networking hooks.
- Diagnostics helpers.
- HUD and UI extension points.
- Blueprint library and runtime subsystem helpers.




## License

Licensed under the [MIT License](./LICENSE).