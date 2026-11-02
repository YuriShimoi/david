# Dither patterns
Pattern files here (and in the config folder's `patterns/`) show up in Effects > Color > Dither > Pattern, by id.

- `.aseprite-extension` packs: `package.json` with `contributes.ditheringMatrices` (`id`, `name`, `path`) next to PNG files.
- Grayscale `.png`: darker = lower threshold; the distinct levels are ranked, so any gray range works.
- `.json` (`[[0, 8, 2, 10], ...]` or `{ "matrix": [...], "max": 15 }`) or `.csv` (one row per line).
