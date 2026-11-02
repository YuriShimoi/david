# Receitas

## Ler cada pixel da seleção (comando Lua)

```lua
run = function(doc)
  local x0, y0, w, h = doc:selectionBounds()
  if not x0 then x0, y0, w, h = 0, 0, doc.width, doc.height end
  local layer = doc:layer()
  for y = y0, y0 + h - 1 do
    for x = x0, x0 + w - 1 do
      if doc:selected(x, y) > 0 then
        local r, g, b, a = david.channels(layer:get(x, y))
        -- ...
      end
    end
  end
end
```

## Um efeito com margem (Lua)

Um efeito que lê os vizinhos precisa de pixels ao redor da seleção. Dê a ele uma margem, e `img:get` poderá ler até essa distância:

```lua
david.effect{
  id = "box-blur-3", name = "Box Blur 3×3", margin = 1,
  run = function(img, p)
    for y = 0, img.height - 1 do
      for x = 0, img.width - 1 do
        local sr, sg, sb, sa = 0, 0, 0, 0
        for dy = -1, 1 do for dx = -1, 1 do
          local r, g, b, a = david.channels(img:get(x + dx, y + dy))
          sr, sg, sb, sa = sr + r, sg + g, sb + b, sa + a
        end end
        img:set(x, y, david.rgba(sr // 9, sg // 9, sb // 9, sa // 9))
      end
    end
  end,
}
```

## Adicionar uma camada e preenchê-la

```lua
david.command{ id = "red-layer", name = "Red Layer", run = function(doc)
  local layer = doc:addLayer("Red")
  layer.blend = "multiply"
  layer.opacity = 128
  for y = 0, doc.height - 1 do for x = 0, doc.width - 1 do layer:set(x, y, david.rgba(255, 0, 0)) end end
end }
```

## Executar um efeito nativo a partir de um comando

```lua
run = function(doc)
  doc:applyEffect("gaussian-blur", { radius = 4 })
  doc:applyEffect("posterize", { levels = 6 })
end
```

## Fazer algo em cada quadro (comando Lua)

Um documento pode ser uma animação. As camadas leem e escrevem no quadro ativo, então um comando percorre os quadros mudando `doc.activeFrame`; continua sendo uma única etapa de desfazer.

```lua
david.command{ id = "flash", name = "Flash Every Other Frame", run = function(doc)
  local layer = doc:layer()
  for f = 2, doc:frameCount(), 2 do
    doc.activeFrame = f
    for y = 0, doc.height - 1 do
      for x = 0, doc.width - 1 do
        local r, g, b, a = david.channels(layer:get(x, y))
        if a > 0 then layer:set(x, y, david.rgba(255, 255, 255, a)) end
      end
    end
  end
end }
```

Pela CLI, `--all-frames` executa os efeitos em todos os quadros, e `--slice 16x16` transforma uma folha de sprites em quadros:

```
david-cli walk-sheet.png --slice 16x16 --fps 10 -o walk.gif
david-cli walk.gif --effect outline:color=#000 --all-frames --sheet cols=8,json -o walk-outlined.png
```

## Processar uma pasta em lote pela CLI

```
david-cli --batch "sprites/*.png" --resize 300% --filter nearest --effect outline:color=#000 --out-dir big/
```

Cada arquivo encontrado vai para `big/` com o mesmo nome. `--format webp` muda o formato.

## Um script em uma imagem

`--script` executa um arquivo Lua na imagem, com `david.image` como documento e os valores de `--arg` em `david.args`:

```lua
-- tint.lua: david-cli in.png --script tint.lua --arg amount=40 -o out.png
local doc = david.image
local amount = tonumber(david.args.amount or "20")
local layer = doc:layer()
for y = 0, doc.height - 1 do
  for x = 0, doc.width - 1 do
    local r, g, b, a = david.channels(layer:get(x, y))
    layer:set(x, y, david.rgba(math.min(255, r + amount), g, b, a))
  end
end
```

## Encadear imagens com pipes

`-` lê da entrada padrão ou escreve na saída padrão (PNG, a menos que `--format` diga outro):

```
cat in.png | david-cli - --effect invert -o - > out.png
```

## Descobrir os parâmetros de um efeito

```
david-cli --list-effects
david-cli --list-effects --json
```
