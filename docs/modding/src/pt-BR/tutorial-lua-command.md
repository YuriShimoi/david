# Tutorial: Palette Swap (comando Lua)

Um comando que troca uma cor por outra na camada ativa, dentro da seleção. Ele pergunta primeiro as duas cores e uma tolerância, e a mudança inteira vira um único passo de desfazer.
O mod pronto está em `mods/examples/palette-swap`.

## 1. mod.json

```json
{
  "name": "Palette Swap",
  "version": "1.0.0",
  "api": 1,
  "kind": "lua",
  "entry": "main.lua"
}
```

## 2. Registrando o comando

`main.lua` roda uma vez quando o mod é carregado. Ele registra coisas por meio da tabela `david`:

```lua
david.command{
  id = "palette-swap",
  name = "Palette Swap",
  menu = "Mods/Color",
  params = {
    { id = "from", label = "Replace", type = "color" },
    { id = "to", label = "With", type = "color" },
    { id = "tolerance", label = "Tolerance", type = "int", min = 0, max = 255, default = 0 },
  },
  run = function(doc, p)
    -- the edit goes here
  end,
}
```

`menu = "Mods/Color"` o coloca em **Mods > Color**. Como tem `params`, o David pede esses valores primeiro em uma janela (os parâmetros de cor começam na cor primária) e depois chama `run(doc, p)` com os valores por id.

## 3. Editando pixels

`doc:layer()` é a camada ativa. As cores são inteiros simples: `david.channels(c)` separa uma em r, g, b, a (0–255), e `david.rgba(r, g, b, a)` as junta.

```lua
run = function(doc, p)
  local layer = doc:layer()
  local fr, fg, fb = david.channels(p.from)
  local tr, tg, tb = david.channels(p.to)
  for y = 0, doc.height - 1 do
    for x = 0, doc.width - 1 do
      local r, g, b, a = david.channels(layer:get(x, y))
      local close = math.abs(r - fr) <= p.tolerance and math.abs(g - fg) <= p.tolerance and math.abs(b - fb) <= p.tolerance
      if a > 0 and close and doc:selected(x, y) > 0 then
        layer:set(x, y, david.rgba(tr, tg, tb, a))
      end
    end
  end
end,
```

`layer:set` grava independentemente da seleção, então o comando verifica `doc:selected(x, y)` por conta própria (vale 255 em todo lugar quando nada está selecionado).

## 4. Desfazer

Você não precisa fazer nada para o desfazer: toda mudança de pixel e de camada dentro de `run` vira um único passo do Histórico com o nome do comando. Se `run` gerar um erro, tudo o que ele mudou é restaurado e a mensagem aparece em uma janela.

## Notas

- Uma chamada que dure mais de 10 segundos é interrompida, para que um erro não possa travar o David.
- Imagens grandes: ler e gravar pixels um a um pelo Lua é lento acima de alguns megapixels. Um efeito (`david.effect`) roda só sobre os limites da seleção; prefira-o para trabalho por pixel.
- A mesma lógica de comando pode rodar pela CLI como script: veja o [livro de receitas](cookbook.html).
