# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.2.1] - Unreleased

### Added
- Vector tool row (vector layers): Mouse, Node, Shapes, Pen (the Curve tool), Line, Pencil and Text lead the tool bar, followed by flip, rotate 90°, group, boolean operations, order, object to path, duplicate and delete.
- Object window (vector support on; opens as a tab next to Colors, View > Vector > Object): name, position, size and angle, corner radius, sides and star points, opacity, fill, and border color, width, position, ends, corners and dash. The Mouse tool's style bar moved here.
- Boolean operations on picked objects: Union, Subtract, Intersect and Exclude make one path with the bottom object's look; curves are refitted and corners kept (Clipper2).
- The Pencil on a vector layer draws a freehand path, fitted to smooth curves when the button lets go.
- Double-click an object with the Mouse tool to edit its nodes; Esc (or a double-click on empty canvas) goes back to the Mouse tool.
- Ctrl+G groups and Ctrl+Shift+G ungroups objects.

### Changed
- The Node tool has its own icon (it shared the Curve tool's).

### Fixed
- Grid snap now applies while moving or resizing with Move Selected Pixels and Move Selection, while moving or scaling vector objects, and while dragging nodes and handles.

## [1.2.0] - 2026-11-02

### Added
- Vector layers (beta, off until Settings > Beta tools > Vector support is on): objects instead of pixels, drawn by ThorVG and sharp at any zoom; Image > Vector > Pixel Preview shows the exact exported pixels. Image > Vector and the Layers panel add a vector layer (Ctrl+Alt+N).
- On a vector layer, Shapes (all kinds, free polygon included), Line, Curve and Text make editable objects, with fill, line (inside, center or outside), dashes, arrow heads and text as glyph outlines.
- The Mouse tool picks objects on vector layers: click, Shift-click or a marquee; drag to move, the corner nubs scale (Shift keeps the ratio), the round nub rotates (Shift snaps to 15°); arrow keys nudge and Del deletes. Its bar edits fill and line color, line width and position, opacity, corner radius, polygon sides and star points, and duplicates, deletes, reorders, groups, ungroups and turns objects into paths.
- Node tool (N, vector layers): pick and drag nodes and their handles (marquee and Shift too), bend a segment by dragging it, double-click a segment to add a node, Del removes nodes; corner, smooth and symmetric nodes; open or close a path. Shapes turn into paths on their first node edit.
- Raster tools, effects, fills and pastes on a vector layer ask first: rasterize the layer or paint on a new layer above it, with "Remember my choice". Layer > Rasterize Layer, and Merge Down into a vector layer, turn it into pixels (undoable).
- SVG: open .svg files, Image > Vector > Import SVG into a vector layer and Export SVG of the whole image (vector layers as objects, raster layers as embedded PNG, each an Inkscape layer). Paths with every command (arcs included), basic shapes, groups, transforms, solid and gradient paint, strokes, opacity and `<use>` are read.
- Copy and paste of vector objects between vector layers; SVG code on the clipboard pastes as objects.
- Image > Vector > Trace Pixels: an exact trace of the active layer into a new vector layer, one shape per same-color region.
- `.david` files keep vector layers as SVG next to their rendered PNG, so 1.1.0 still opens them (as pixels). Resize Image and Canvas Size scale and move the objects too.
- Lua: `doc:addVectorLayer`, `layer.isVector`, `layer:objects`, `layer:addObject`, `layer:setObject`, `layer:removeObject`, `layer:svg`, `layer:addSvg`; the Star Generator example mod and a "Vector objects" page in the modding guide (English and Portuguese).
- CLI: SVG input and output (`david-cli icon.svg --resize 256x256 -o icon.png`) and `--trace pixel`.

## [1.1.0] - 2026-11-02

### Added
- Cut (Ctrl+X), Copy (Ctrl+C), Copy Merged (Ctrl+Shift+C), Paste (Ctrl+V), Paste into New Layer (Ctrl+Shift+V) and Paste into New Image (Ctrl+Alt+V) in the Edit menu, through the system clipboard as PNG (with alpha) and BMP. A paste floats on Move Selected Pixels at the top-left of the visible area until Enter or another tool.
- Pasting an image larger than the canvas asks whether to expand the canvas or keep its size.
- Perlin Noise and Fractal Noise (detail, roughness, turbulence) in Effects > Noise, from the primary to the secondary color or in grayscale, with opacity and seed.
- Settings > Beta tools, with a Vector support section (off by default) for the vector features of the next version.

### Changed
- Version 1.1.0.
- The Contour tool (U) is now the Free polygon option of the Shapes tool (A cycles to it): drawn freehand, then it keeps the box handles to move and resize until Enter, with outline, fill or both like the other shapes.
- Spray moves from N to U.
- Dither moves from Effects > Color to Effects > Distort.
- Color window palette: left click sets the color being edited (primary or secondary), right click the other one.
- The Symmetry button uses the vertical-axis icon, and the Symmetry and Tiled menus open right under their buttons.
- Settings and theme files are saved atomically (a crash mid-save can't cut them), and an unreadable `settings.json` is kept as `settings.json.bad` before defaults replace it.

### Fixed
- Line, Curve, Shapes, Text and Gradient follow symmetry.

### Removed
- The small adjustments icon before the tolerance slider of the Magic Wand and Paint Bucket.

### Removed
- The gradient tool's dither select: plain gradients render smooth, and Pixel art mode keeps its Bayer pattern.

## [1.0.0] - 2026-11-01

### Added
- Release packages: a Windows installer (per user, no admin rights, with an uninstaller), a Windows portable zip (settings next to the program), a Linux AppImage and `.tar.gz`; `tools/package.py` builds them.
- Portable mode: a `portable` file next to the program keeps settings, themes and mods in a `config` folder beside it.
- App icon (colored) on the exe, the window, the installer and the AppImage, and its white version in the UI (Help > About and the About dialog); version info on the exe; About and `david-cli --version` show 1.0.0.
- README: what David is, its features, a direct installer download link and the other release files.
- Brazilian Portuguese (Settings > Appearance > Language): the interface, the modding guide (Help > Modding guide opens it in the UI language, with an EN / PT switch on every page) and `README.pt-BR.md`. Effect names, effect settings, blend modes, tool tooltips and joined tool names in Shortcuts are translated too; `tools/gen-lang.py` keeps every language file in step with `en.json`.
- About dialog with a title bar, close button, version and license rows and an OK button.
- The window opens maximized.
- `build.cmd` finds vcpkg from `DAVID_VCPKG`, `VCPKG_ROOT`, a `vcpkg` folder next to the repo, or the copy bundled with Visual Studio.
- Settings: "Reset all settings" at the end of the page list puts every setting back to the defaults (theme, language, shortcuts, tool options, tool strip, layout, mods on/off) after a warning; images, theme files and mods are kept.
- Settings > Tools > Tool strip: show or hide each tool in the left strip and drag the rows to change their order (hidden tools keep their shortcuts); Reset puts the default strip back.
- Timeline: an Export Animation button.
- Replace toggle on the tool bar (shared options): the Pencil, Paintbrush, Spray and Contour put the color in place of the pixel, alpha included, instead of blending over it; soft edges mix. Saved with the settings.
- Mods can add windows (`david.window`): tool windows that dock like Layers and History, and floating dialogs like Settings, drawn with a small widget set (text, headers, buttons, check boxes, sliders, choices, colors, text fields). They're listed in the Mods menu and stay open across sessions. New example mod: Canvas Info; new guide page: Lua windows.
- Animation:
  - Frames: every layer has an image (cel) per frame. New, duplicate, linked (shared cels), delete, drag to move, reverse, per-frame duration and frame rate, tags (name, range, direction, color), link / unlink / clear cel; every change undoes, and undo shows the frame it was made on.
  - Timeline (F9): a bottom tool window like Colors, Layers and History (docked or floating, top-right toggle), with playback buttons, fps and ms fields, loop mode (loop, once, ping-pong, inside the current tag) and tags.
  - Animation Preview window that loops while you draw.
  - Onion skin (Alt+O): frames before and after, Piskel defaults (1 and 1, 20 %, no tint); options for counts, opacity, red/blue tint, whole frame and above/below.
  - Layer onion (Alt+L, in the Layers window): the layers next to the active one ghosted; the others hidden or dimmed.
  - `,` / `.` previous / next frame (not while Line or Curve is the tool), Home / End, Alt+N new frame, Alt+D duplicate.
  - Effects can run on all frames (or the selected ones) as one undo step.
  - Files: `.david` keeps frames, linked cels, durations and tags; GIF, APNG and animated WebP open and save; File > Import Sprite Sheet and Export Animation (GIF, APNG, WebP, sprite sheet with Aseprite JSON, PNG sequence); Layers to Frames and Frames to Layers.
  - CLI: `--frame`, `--all-frames`, `--fps`, `--tag`, `--scale`, `--loops`, `--sheet`, `--slice`; `info` lists frames and tags.
  - Lua: `doc:frameCount()`, `doc.activeFrame`, `doc.fps`, `doc:frameDuration()` / `setFrameDuration()`, `doc:addFrame()`, `doc:deleteFrame()`, `doc.tags`.
- Dither Blur (Effects > Blurs): a Gaussian blur dithered back to the image's own colors (or the current palette) with a Bayer or blue-noise pattern; soft alpha edges dither too, or stay soft.
- Phase 7 (mods, docs, CLI):
  - Mods in three tiers, each a folder with `mod.json`: GLSL shader effects (`// @param` lines become the dialog), Lua 5.4 in a sandbox (effects, commands with dialogs and one undo step, painting tools with a Lua ink), and native plugins through the C header `sdk/david_plugin.h` (effects and file formats). Settings > Mods turns them on and shows their errors and logs; shader and Lua mods reload when their files change.
  - Four example mods in `mods/examples/`: Scanlines, Palette Swap, Checker Brush, a QOI-like file type.
  - `david-cli`: convert, effect chains (`--effect id:k=v`), Lua scripts with `--arg`, stdin/stdout with `-`, batch into a folder, resize, `info --json`, `--list-effects`, `--list-formats`; exit codes 0/1/2/3.
  - Layered files: `.david` (zip with doc.json and a PNG per layer, palette included) and OpenRaster `.ora`; also WebP, JPG, BMP, TGA, QOI and GIF (open). Open and Save As pick the format by extension.
  - The modding guide (`docs/modding/`, Help > Modding guide): getting started, four tutorials, concepts, cookbook, a reference generated from the code, built-in effects, formats and versioning, with search.
- Phase 6 (effects):
  - Layer blend modes (Paint.NET's 14) in Layer Properties, the same on the canvas, in the flattened image and in Merge Down.
  - Adjustments with Paint.NET's shortcuts: Auto-Level, Black and White, Brightness / Contrast, Curves (point editor per channel), Hue / Saturation, Invert Colors, Invert Alpha, Levels, Posterize, Sepia.
  - Effects: Gaussian Blur, Sharpen, Add Noise, Pixelate, Drop Shadow, and Layers > Rotate / Zoom.
  - Dither (Effects > Color): current palette, a palette file, N colors from the image or 1-bit; Bayer, blue noise, custom patterns and four error-diffusion methods; amount and dither band sliders, OKLab or sRGB matching, alpha options, pattern scale and offset.
  - Dither patterns from `patterns/` (app and config folders): Aseprite `.aseprite-extension` packs, grayscale PNG, JSON or CSV matrices.
  - Effect dialogs only show the options that apply.
- Phase 5 (pixel art):
  - Tiled mode (wrap X, Y or both): wrapped, dimmed copies around the image, and strokes that cross an edge continue on the other side. A Tile Preview window shows the image 3×3 (wheel zooms, double-click fits).
  - Symmetry (left/right, top/bottom, both), with axis nubs on the canvas to move each axis.
  - The pixel-perfect option also drives the Pencil: the middle pixel of an L corner is put back.
  - Custom grid (cell size and offset, Ctrl+'), snap for selection, line, curve, shape and gradient points, and a double-click with Rectangle Select selects the grid cell.
  - Palette window: load and save GPL, JASC PAL and HEX, sort by hue or lightness, palette from image (median cut in OKLab), edit swatches (double-click) and drag them to reorder. The Colors window shows the same palette, saved with the settings.
  - Shading ink on the Paintbrush and Pencil: a pixel whose color is a swatch steps to the next one (right click: the previous one).
  - Effects framework: an effect lists typed params, and its dialog is built from them with a live preview; Ctrl+F repeats the last effect. The CLI form `id:k=v` is parsed from the same list.
  - Outline effect (Effects > Object): outside or inside, 4- or 8-connected, thickness 1–16, any color.
  - Autotile (Image > Autotile): a border tile and a floor tile side by side become the 48-tile 12×4 blob sheet on a new layer (C++ port of F-220); the canvas grows first when it is too small.
  - Spray tool (N) with a density setting, and Contour tool (U), a freehand outline filled on release.
  - Menus: Image, Layers, Adjustments, Effects, View > Pixel Art.
- Tool icons from Tabler (outline), with Phosphor for Gradient and Color Picker: copied into `assets/fonts/david-icons.ttf` by `tools/gen-tool-icons.py` (`ui/ToolIcons.h`), also used for the tool cursors.
- Rotating a selection: Move Selected Pixels and Move Selection show the selection's box with a round handle above it (drag to rotate, Shift snaps to 15°), plus an angle field and ±90° buttons on the tool bar.
- Resizing a selection: corner nubs on the Move tools' box (Shift keeps the proportions, whole-pixel sizes while unrotated), plus width/height percent fields on the tool bar.
- Finishing a selection (drag, wand click, closed polygon) switches to Move Selected Pixels, unless Ctrl/Alt was held to combine; Settings > Tools > Selection turns it off.
- Look review 4:
  - Closing with unsaved changes asks in the system's own dialog: Cancel, Save and close, Close (Save / Don't save for New and Open).
  - The tools are a fixed strip on the left (no window, no header); a second column only when the window is too short.
  - Antialiasing and pixel perfect are global options at the end of the tool bar, after a divider.
  - Line: a middle point is always there; it is either a corner (start to middle to end) or the point a curve passes through.
  - Curve: dragging from the first node when closing shapes that node's curve; the release finishes.
  - Shapes: edge nubs resize one side.
- Look review 3:
  - Edit > Resize Image (`Ctrl+R`) and Canvas Size (`Ctrl+Shift+R`), laid out like Paint.NET: by percentage or absolute size, aspect lock, resolution and print size; resampling (best quality, bicubic, bilinear, nearest neighbor, supersampling) with gamma-correct filtering; canvas anchor grid and fill. Both undo in one step.
  - Mouse tool (`V`): clicks do nothing; wheel zoom and middle-button / Space pan as always.
  - Reset View (`Ctrl+Shift+B`) and its button next to the zoom controls.
  - Window toggles in the top-right corner, lit while the window is open.
  - Pointer feedback: the hand over anything clickable, move and resize cursors while dragging, a crosshair for selections and shapes, an I-beam for text, and the painting tools' own icons as their cursors.
  - Line tool: straight or curved, with a middle nub that bends the line.
  - Curve tool (separate from Line): the first drag places two nodes like a line; clicking the first node closes the path and finishes it.
  - Settings > Appearance: a reset button per shape value, back to the theme's own value.
  - Debug builds write a symbolized stack to `%TEMP%\david-assert.txt` on a failed assert or a crash.
- Phase 4, smart tools:
  - Magic Wand (`S` cycle) and a new fill engine for the wand and Paint Bucket: OKLab tolerance (0% = exact color, "Classic RGB" kept), soft antialiased edges, stop at edges (Sobel on lightness), fill to outline (secondary color), and a parallel global mode.
  - Gradient tool (`G`): linear, reflected, diamond, radial, conical and two spirals; sRGB, linear RGB, OKLab and OKLCH (shorter or longer hue) interpolation; Bayer or blue-noise dithering; transparency mode; pixel-art mode that uses only the stop colors; a stop bar to add, move, remove and recolor stops. Nubs stay editable until Enter.
  - Line / Curve tool (`O`): Shift snaps to 15°, Alt draws from the center; Bezier nodes with symmetric or broken handles; dashes and arrow heads (`/`, `,`, `.`); a 1-px pixel-perfect mode.
  - Shapes tool (`O` again): rectangle, rounded rectangle, ellipse and polygon (`A` cycles); outline, fill or both; corners and body stay draggable until Enter.
  - Text tool (`T`): FreeType + HarfBuzz text typed on the canvas, with a font list from DirectWrite or fontconfig that searches by name, previews each family in its own font, and pins favorites and recent fonts. Size, bold and italic (synthesized when the family lacks them), alignment and antialiasing.
  - The new tool options are saved with the settings; self-test round 3 covers the tools.
- Look review round 2:
  - Floating windows snap to each other's sides and to the work area's edges while dragged, and their places and sizes are saved in `settings.json`.
  - Floating Tools is fixed at two tools wide; floating Layers is fixed to fit its button row.
  - The icon font is aligned to the text's capital height at startup, so icons and labels sit on one line.

### Changed
- Dialogs, menus, Settings and floating mod windows always have a 1 px border in the theme's line color; docked panels and the bars keep the theme's border.
- Onion skin is on by default.
- Image menu: Resize Image and Canvas Size on top; Rotate / Zoom with the zoom commands; Autotile at the bottom.
- Classic theme: softer whites by default (background `#F6F6F6`, surface `#EDEDED`, raised `#D8D7D7`, line `#D1D1D1`, hover `#CBDEED`).
- Menus: the Layers menu is removed (Rotate / Zoom moves to Image); View is merged into Image; Settings moves into File, just before Exit.
- Effect and adjustment dialogs no longer dim the window, so the preview can be judged; mods turn the dim on with `dim = true` (Lua) or `// @dim` (shader).
- Number options in effect and adjustment dialogs are bar sliders with a number field, like the Colors window's RGB / HSV rows; Hue, Saturation, Brightness, Lightness and the Levels points show their color range.
- Resize Image and Canvas Size keep the layer's blend mode.
- libwebp is built with its `mux` feature (animated WebP).
- Tool option icons (options bar, pixel-art bar) moved from Font Awesome to Tabler and Phosphor, like the tool strip; the select modes use Phosphor's square set.
- A dragged symmetry axis snaps to the image center within 8 screen px and then stays centered on resize; Alt drags freely.
- Dev theme uses the same 4 px rounding as Light.
- Colors window: the advanced toggle became a small "RGB · HSV · A" hint under the palette; the band pointers are much smaller.

### Changed
- The Light theme is now called Classic (`themes/classic.theme.json`); a saved `"theme": "light"` still opens it.
- David never changes the system cursor (no hidden cursor over the canvas, no resize or hand cursors); the brush circle and pencil crosshair are drawn next to the normal pointer.

### Changed
- Line: one "Sharp corner" toggle, off by default; off, the line curves through its middle point.
- Dialogs: OK (and the main action) has a filled accent button, Cancel a filled neutral one; Resize Image and Canvas Size have an X in the title bar.
- The unsaved-changes question is David's own dialog again (styled like the others) with an X, Cancel, Save and close, Close.
- Tool bar: no current-tool icon at its start (the tool strip shows it); the global options follow the tool's options with normal spacing.
- Top-right window toggles: a little padding on the right, and the hover color shows over the open color.
- David starts on the Mouse tool.
- Default layout: Colors on the top right, Layers and History as tabs below it (Layers open). The layout file is now `layout-2.ini`, so the new layout applies once.
- Window toggles in the top-right corner show their state only by icon color.
- Colors window: the hex field stays right under the picker; the palette moves to the picker's right when there is room for it.
- Gradient: the pixel-art toggle has its own icon (the checkerboard is pixel perfect now).
- The four selection tools are one Select tool; the shape (rectangle, ellipse, lasso, polygon) is a tool bar option, and `S` still cycles through them before moving on to the Magic Wand.
- The canvas pans freely in every direction (a 48 px strip of the image always stays in view), and keeps its center when panels open, close or resize.
- The Layers menu is gone (the Layers window has every command); Mods sits just before Help.
- Settings: the padding value is removed from themes.

### Fixed
- Windows build: a changed icon file now recompiles the exe's resources (the exe kept the old icon).
- Export Animation and Import Sprite Sheet: the fields had the dialog's own color and didn't show; they take the darker field color like Canvas Size.
- New Image: a title bar with a close button, Width and Height labels, and OK / Cancel like the other dialogs.
- The pixel grid showed on small images (high zoom) with the grid off; it now needs the Grid toggle on, plus the zoom set in Settings.
- Zoom tool: a drag that starts or ends outside the image is clamped to the image's edges.
- Canvas Size: every anchor cell has a fill, so the 3×3 grid shows (the flat buttons were invisible until hovered).
- Debug builds on Windows: the Lua API file is compiled with `/bigobj`.
- David crashed on exit with Lua mods loaded: mod tools and commands freed their Lua functions after the Lua state was closed. Those functions now keep their state alive.
- Layer thumbnails point-sampled the layer, so thin lines and small details came and went; they are now an area average drawn 1:1 on whole pixels.
- Colors window: the RGB / HSV bands showed dashed checkerboard lines along their top and bottom edges.
- Closing the window asked twice (the close button sends both a close request and a quit event); a question already waiting now swallows the second one.
- Colors window flipped between two sizes at some widths: the picker's width followed the scrollbar, and its height the width. Widths now always leave room for the scrollbar.
- Shapes: dragging a corner moved the whole shape (the opposite corner was recomputed while dragging); it now resizes.
- Settings: the theme font size now changes every text, not only some (ImGui resynced the size mid-frame).
- Tool keys `G`, `T` and `O` did nothing (the tool shortcut list was hard-coded); it now comes from the tools.
- Linux: UI fonts with their own private-use glyphs (DejaVu) no longer hide icons (the Zoom tool showed a box).
- List rows (History, Layers, Settings pages, theme list) share one row widget: exact highlight, left padding, content centered on the row.
- Colors window: the picker-type switch is a plain icon of the current shape in the top-right corner.
- Theme preview card: removed the status dots, laid it on one grid so the OK button lines up with the layer row's right edge, and sized it to its content.
- Settings > Shortcuts showed garbage text (a translated `std::string` passed to a printf-style call); `tr()` now only takes `const char*`.
- Look review round 1:
  - Every UI string goes through a translation table; `lang/en.json` is the built-in English file (made by `tools/gen-lang.py`), and Settings > Appearance picks the language.
  - Floating windows option (Settings > Appearance): Paint.NET-style floating, resizable tool windows instead of docked ones, switchable live.
  - Colors window: square picker as an alternative to the hue wheel, and an advanced mode with hex plus R, G, B, H, S, V and alpha bands.
  - Mods menu with a disabled "Modding guide" item, ready for phase 7.

### Changed
- Panel titles show the name next to the icon when the panel is wide enough; the Tools window lists tool names when they fit.
- Buttons are flat (no fill or border until hovered) and icons are centered on their drawn shape.
- Theme preview: the selected tab's accent line follows its rounded corners, and the OK button text is centered.
- Phase 3, settings and themes:
  - Settings window (Alt+X or the Settings menu) with Appearance, Tools, Canvas, Shortcuts and Paths pages; changes apply live and are saved to `settings.json` in the config folder.
  - Light theme (Windows 10/11 light colors, 4 px radius, 1 px borders) next to Dev.
  - Theme files hot-reload within a second of being edited; user themes live in `<config>/themes/` and override built-in ones of the same name.
  - Theme list with swatches and a hover preview; new (duplicate), import, export, reset to built-in, delete; a token editor with WCAG contrast warnings; Windows system accent toggle.
  - Shortcut remapping with search, key capture, removal, per-command and global reset; only differences from the Paint.NET defaults are saved.
  - Canvas options: checkerboard size, pixel grid threshold, wheel-zoom swap, history memory cap, default new image size.
- Phase 2, the Paint.NET core:
  - Tools (Paint.NET keys, S and M cycle): Rectangle, Ellipse, Lasso and Polygon Lasso select; Move Selected Pixels and Move Selection (right drag rotates, Shift snaps 15°, Ctrl copies); Zoom; Pan; Paint Bucket (tolerance, contiguous/global, layer/image sampling); Paintbrush and Eraser (antialiasing, pen pressure to size/opacity); Pencil; Color Picker (sample size, layer/image, switch back).
  - Strokes keep a coverage map, so a semi-transparent stroke never builds up over itself.
  - Selection as an 8-bit mask with replace/union/exclude/intersect/xor (Ctrl, Alt, right button), marching ants, Select All, Deselect, Invert, Erase (Del) and Fill (Backspace) selection; every edit is clipped by it.
  - Layers: add, delete, duplicate, merge down, move up/down, properties (name, visibility, opacity), thumbnails; saving flattens visible layers.
  - Tools (F5), History (F6, click a step to jump), Layers (F7) and Colors (F8, hue wheel, hex, palette) windows, docked by default, with Ctrl+Shift+F5–F8 to reset; a tool bar with the current tool's options.
  - Pen input through SDL3 pen events, and a Pen Probe window (View menu).
  - Every edit, layer change and selection change is one History step.
- Phase 1 skeleton (C++20, CMake presets `win-*`/`linux-*`, vcpkg manifest pinned to a baseline; static CRT build with no DLLs):
  - Window with an OpenGL 3.3 canvas, ImGui docking, a File/Edit/View/Help menu and a status bar (cursor position, size, zoom).
  - Sparse 256×256 RGBA8 tiles with a GPU texture per tile, uploaded only when changed; checkerboard, nearest filtering zoomed in, mipmaps zoomed out, pixel grid above 800%.
  - Zoom anchored on the cursor (Ctrl+wheel, Ctrl +/−), Ctrl+B zoom to window, Ctrl+0 / Ctrl+Shift+A actual size; pan with Space+drag, middle-drag and the wheel.
  - A temporary 1-px test pencil (left primary, right secondary).
  - Tile-diff undo/redo (Ctrl+Z / Ctrl+Y) with a memory cap.
  - PNG open/save through libpng with native dialogs, drag-and-drop, New (800×600 white) and an unsaved-changes prompt.
  - Dev theme loaded from `themes/dev.theme.json`, with the system UI font and Font Awesome icons.
  - Redraws only on input, so idle CPU stays at 0%.
  - `david_tests` (core checks) and the `--shot`/`--selftest`/`--dev` dev flags.
