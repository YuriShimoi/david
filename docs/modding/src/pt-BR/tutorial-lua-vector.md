# Objetos vetoriais: Gerador de estrelas

Camadas vetoriais guardam objetos em vez de pixels: retângulos, elipses, polígonos e estrelas, caminhos e texto, cada um com preenchimento, linha e transformação. Eles ficam nítidos em qualquer zoom e continuam editáveis com as ferramentas Mouse e Nós. O suporte a vetores é um recurso beta: ative em **Configurações > Ferramentas beta > Suporte a vetores**.

Este tutorial cria um comando que adiciona uma fileira de estrelas a uma camada vetorial. O mod pronto é `mods/examples/star-generator`.

## 1. Objetos pelo Lua

Os objetos de uma camada vetorial são tabelas simples. `layer:objects()` lista todos (de baixo para cima), `layer:addObject(fields)` adiciona um no topo e devolve o id dele, `layer:setObject(id, fields)` muda alguns campos e `layer:removeObject(id)` apaga.

| Tipo | Campos |
|---|---|
| `rect` | `x`, `y`, `w`, `h`, `radius` (cantos arredondados) |
| `ellipse` | `x`, `y`, `w`, `h` (a caixa em volta) |
| `polygon` | `x`, `y`, `w`, `h`, `sides`, `inner` (abaixo de 1 vira estrela) |
| `path` | `d`: dados de caminho SVG, como `"M0 0 L10 0 L10 10 Z"` |
| `text` | `text`, `font` (um arquivo de fonte), `size`, `x`, `y` |

Todos os tipos também aceitam `name`, `fill` e `stroke` (cores de `david.rgba`, ou `false` para nenhuma), `width` (a largura da linha), `opacity` (de 0 a 1) e `transform` (uma lista de transformações SVG, como `"rotate(45 50 50)"`).

## 2. mod.json

```json
{
  "name": "Star Generator",
  "version": "1.0.0",
  "api": 1,
  "kind": "lua",
  "entry": "main.lua"
}
```

## 3. O comando

```lua
david.command{
  id = "star-generator",
  name = "Star Generator",
  menu = "Mods/Vector",
  params = {
    { id = "count", label = "Stars", type = "int", min = 1, max = 20, default = 3 },
    { id = "points", label = "Points", type = "int", min = 3, max = 24, default = 5 },
    { id = "inner", label = "Inner radius %", type = "int", min = 10, max = 100, default = 45 },
    { id = "color", label = "Fill", type = "color", default = 0 },
  },
  run = function(doc, p)
    -- Draw on the active layer when it is a vector layer, else on a new one
    local layer = doc:layer()
    if not layer.isVector then layer = doc:addVectorLayer("Stars") end
    -- One row across the middle of the image
    local cell = doc.width / p.count
    local size = math.min(cell, doc.height) * 0.8
    for i = 0, p.count - 1 do
      layer:addObject({
        kind = "polygon",
        name = "Star",
        x = cell * i + (cell - size) / 2, y = (doc.height - size) / 2, w = size, h = size,
        sides = p.points, inner = p.inner / 100,
        fill = p.color,
        stroke = david.rgba(0, 0, 0), width = math.max(1, size / 40),
      })
    end
    david.log("star generator: " .. #layer:objects() .. " objects on " .. layer.name)
  end,
}
```

`layer.isVector` diferencia uma camada vetorial de uma raster, e `doc:addVectorLayer(name)` adiciona uma acima da camada ativa. Tudo o que o comando adiciona (a camada e as estrelas) é um único passo de desfazer, como em qualquer outro comando.

## 4. SVG de entrada e saída

`layer:svg()` devolve os objetos de uma camada vetorial como um documento SVG, e `layer:addSvg(text)` adiciona no topo os objetos de um documento SVG. É o jeito fácil de trazer formas feitas em outro lugar:

```lua
layer:addSvg('<svg xmlns="http://www.w3.org/2000/svg"><circle cx="20" cy="20" r="10" fill="red"/></svg>')
```

Pela linha de comando, o `david-cli` também lê e grava SVG: `david-cli icon.svg --resize 256x256 -o icon.png` renderiza um SVG nítido em qualquer tamanho, e `david-cli sprite.png --trace pixel -o sprite.svg` transforma pixel art em uma forma por região de cor.

## 5. Teste

Ative o suporte a vetores, abra qualquer imagem e rode **Mods > Vector > Star Generator**. Selecione as estrelas com a ferramenta Mouse para mover, redimensionar, girar ou mudar a cor, ou dobre-as com a ferramenta Nós (N).
