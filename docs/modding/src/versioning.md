# Versioning

## The API version

David has one API version for all three tiers. It is **1**.

- `mod.json`'s `"api"` says the version a mod needs. David refuses mods that need a newer version than its own, with a clear error.
- Lua mods can read `david.apiVersion`; native plugins get `host->api_version`.

## The policy

- New functions, fields and parameters can arrive in the same version. Mods should ignore what they don't know.
- Removing or changing anything raises the version. A deprecated feature keeps working for at least one more version and is listed below first.
- Effect ids (`gaussian-blur`, `dither`, ...) and their parameter ids are part of the API, because CLI scripts use them.
- The native ABI only grows at the end of its structs; a version bump comes with a new `david_plugin.h`.

## Changes

| Version | Changes |
|---|---|
| 1 | First version: shader effects with `@param`, the Lua `david` API (effects, commands, tools, documents, layers), `david_plugin.h` (effects, file formats), CLI `--effect`, `--script`. |
| 1 (added) | Animation frames: `doc:frameCount()`, `doc.activeFrame`, `doc.fps`, `doc:frameDuration()` / `setFrameDuration()`, `doc:addFrame()`, `doc:deleteFrame()`, `doc.tags`. Layers read and write the active frame, so older scripts work on whichever frame is showing. CLI `--frame`, `--all-frames`, `--fps`, `--tag`, `--scale`, `--loops`, `--sheet`, `--slice`. |
| 1 (added) | Effect dialogs no longer dim the window behind them; an effect asks for the dim with `dim = true` (Lua) or `// @dim` (shader). |
| 1 (added) | Windows: `david.window{...}` (tool windows that dock, and dialogs), the `ui` widgets, `david.openWindow` / `closeWindow`. |

## Deprecated

Nothing yet.
