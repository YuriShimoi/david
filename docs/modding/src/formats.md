# Pattern, theme and file formats

These aren't code, but they are ways to extend David.

## Dither patterns

**Effects > Color > Dither** with the method **Pattern** uses a threshold matrix. David ships Bayer 2×2, 4×4, 8×8 and blue noise, and loads more from the `patterns` folder next to the program and in your config folder. A pattern is referred to by its id, also from the CLI: `--effect dither:method=Pattern,pattern=my-pattern`.

| File | Id | Format |
|---|---|---|
| `.png` | the file name | A grayscale image; darker means a lower threshold. The distinct gray levels are ranked, so any range works (0–3, 0–255, ...). |
| `.json` | the file name | `[[0, 8, 2, 10], [12, 4, 14, 6], ...]`, or `{ "matrix": [...], "max": 15 }` when the values don't reach their maximum. |
| `.csv` | the file name | One row per line, values separated by commas. |
| `.aseprite-extension` | each matrix's `id` | Aseprite's extension zip: a `package.json` with `contributes.ditheringMatrices` (`id`, `name`, `path`) next to the matrix files. The same pack works in Aseprite and David. |

A value v in a matrix whose largest value is max becomes the threshold (v + 0.5) / (max + 1).

## Themes

A theme is one JSON file in the `themes` folder (built in) or your config folder's `themes` (yours). Settings > Appearance makes a copy for you to edit, and David reloads it when the file changes.

```json
{
  "name": "Dev",
  "colors": { "bg": "#1A1A1A", "surface": "#212121", "accent": "#8BE9FD", "...": "..." },
  "shape": { "radius": 4, "borderWidth": 0, "spacing": 6, "fontSize": 15, "font": "" }
}
```

| Token | Used for |
|---|---|
| `bg`, `surface`, `raised` | Window background, panels, inputs and buttons |
| `line`, `hover`, `pressed` | Dividers and neutral fills, and their states |
| `text`, `textMuted` | Text and secondary text |
| `accent`, `accentHover`, `onAccent` | The one highlight color, its hover, and text on it |
| `good`, `warn`, `bad` | Status colors |
| `canvasBg`, `checkerA`, `checkerB` | Around the image, and the transparency checkerboard |
| `selectionAnts`, `nub` | Marching ants and on-canvas handles |
| `radius`, `borderWidth`, `spacing`, `fontSize`, `font` | Corner radius, border width, item spacing, UI font size and font file |

## The .david file

A zip: `doc.json`, `layers/0.png`, `layers/1.png`, ... (bottom first) and `merged.png` (the flattened image, for previews).

```json
{
  "app": "david", "version": 1, "width": 64, "height": 64, "dpi": 96, "active": 1,
  "palette": ["1a1c2c", "5d275d"],
  "layers": [
    { "name": "Background", "visible": true, "opacity": 255, "blend": "normal", "file": "layers/0.png" }
  ]
}
```

David also reads and writes OpenRaster (`.ora`), which Krita, GIMP and MyPaint open. Blend modes that OpenRaster lacks are stored as `david:<mode>`, which other programs read as normal.
