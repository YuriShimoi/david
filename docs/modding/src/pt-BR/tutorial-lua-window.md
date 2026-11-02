# Tutorial: Canvas Info (janelas Lua)

Uma janela de ferramenta que encaixa ao lado de Camadas e mostra o tamanho, as camadas e os quadros da imagem, além de uma caixa de diálogo que ela abre para renomear as camadas. O mod pronto está em `mods/examples/canvas-info`.

## Dois tipos de janela

- **Janela de ferramenta** (`kind = "tool"`, o padrão): encaixa como Cores, Camadas e Histórico. Ela abre primeiro como uma aba ao lado de Camadas; arraste a aba para encaixá-la em outro lugar. Com Configurações > Aparência > janelas flutuantes ativado, ela flutua como as outras janelas.
- **Diálogo** (`kind = "dialog"`): flutua sobre a tela como as Configurações e nunca encaixa. Esc fecha.

Os dois aparecem no menu Mods (marcados enquanto abertos), e o David lembra quais estavam abertos na próxima sessão.

## A janela de ferramenta

O `mod.json` é o mesmo do [tutorial de comando](tutorial-lua-command.html), com `"entry": "main.lua"`. Depois:

```lua
local count = 0

david.window{
  id = "canvas-info",
  title = "Canvas Info",
  draw = function(ui, doc)
    ui:header("Image")
    ui:text(doc.width .. " x " .. doc.height .. " px")
    ui:text(doc:layerCount() .. " layers, " .. doc:frameCount() .. " frames")

    ui:header("Active layer")
    local layer = doc:layer()
    ui:text(layer.name)
    if ui:button("Count opaque pixels", -1) then
      count = 0
      for y = 0, doc.height - 1 do
        for x = 0, doc.width - 1 do
          local _, _, _, a = david.channels(layer:get(x, y))
          if a > 0 then count = count + 1 end
        end
      end
    end
    ui:muted(count .. " opaque pixels at the last count")

    if ui:button("Rename Layers...", -1) then david.openWindow("rename") end
  end,
}
```

`draw(ui, doc)` roda **a cada quadro** enquanto a janela está aberta (modo imediato): ela reconstrói o conteúdo toda vez a partir do documento e das variáveis do próprio mod. Mantenha-a leve; faça o trabalho pesado só quando um botão for pressionado, como a contagem de pixels faz aqui.

Os widgets são dispostos de cima para baixo. Um botão retorna `true` no quadro em que foi clicado; `-1` como largura preenche a linha.

## O diálogo

```lua
local prefix, numbered, start = "Layer", true, 1

david.window{
  id = "rename",
  title = "Rename Layers",
  kind = "dialog",
  width = 360,
  height = 200,
  draw = function(ui, doc)
    prefix = ui:input("Prefix", prefix)
    numbered = ui:checkbox("Number them", numbered)
    if numbered then start = ui:slider("Start at", start, 0, 100) end
    ui:separator()
    if ui:button("Apply", 100) then
      for i = 1, doc:layerCount() do
        doc:layer(i).name = numbered and (prefix .. " " .. (start + i - 1)) or prefix
      end
      ui:close()
    end
    ui:sameLine()
    if ui:button("Cancel", 100) then ui:close() end
  end,
}
```

Os widgets de valor recebem o valor atual e retornam o novo (mais `true` como segundo resultado quando ele mudou), então o padrão é sempre `value = ui:widget("Label", value)`. Os rótulos ficam à esquerda, como nos diálogos de efeito; o controle deslizante é a mesma barra com um campo numérico.

`width` e `height` são o tamanho com que a janela abre da primeira vez; depois disso o David mantém o tamanho que o usuário definiu.

## Desfazer

Tudo o que uma chamada de `draw` altera no documento é **uma única etapa de desfazer**, com o nome da janela (aqui "Rename Layers"). Por isso, altere o documento ao pressionar um botão, e não enquanto um controle deslizante se move, ou cada quadro do arrasto vira uma etapa própria.

## Quando algo dá errado

Se `draw` gerar um erro, a janela mostra o erro no lugar do conteúdo e o erro vai para o log do mod (Configurações > Mods). Corrija o arquivo e salve: o mod recarrega e a janela volta a ser desenhada.

## Widgets

| Chamada | Retorna |
|---|---|
| `ui:text(s)`, `ui:muted(s)`, `ui:header(s)` | |
| `ui:separator()`, `ui:spacing()`, `ui:sameLine()` | |
| `ui:button(label [, width])` | pressionado |
| `ui:checkbox(label, value)` | valor, mudou |
| `ui:slider(label, value, min, max [, step])` | valor, mudou |
| `ui:choice(label, index, { names })` | índice (a partir de 1), mudou |
| `ui:color(label, color)` | cor, mudou |
| `ui:input(label, text)` | texto, mudou |
| `ui:width()` | a largura restante na linha |
| `ui:close()` | |

Dois widgets com o mesmo rótulo não são problema; o texto depois de `##` em um rótulo não é exibido. A [referência da API](reference.html#lua-ui) tem os detalhes.
