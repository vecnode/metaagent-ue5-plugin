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
6. If the pawn is missing camera rig parts, runtime camera fallback is added.
7. Keyboard movement works through fallback input (walk + sprint).

## Minimum Setup

- Startup GameMode: AMetaAgentGameMode (or derived BP).
- Placed actor name: MAIN_CHARACTER.
- Keep strict possession settings:
	- bRequireExactPreferredPawnName=True
	- bRequireUniquePreferredPawnName=True
	- bAllowSpawnFallback=False

## Current Boundaries

- Anything beyond possession + camera fallback + movement is work in progress and intentionally not documented here yet.

## License

Licensed under the ![MIT License](./LICENSE)