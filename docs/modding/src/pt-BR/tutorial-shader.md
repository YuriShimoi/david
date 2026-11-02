# Tutorial: Scanlines (efeito de shader)

Vamos escrever um efeito que escurece linhas como uma tela CRT antiga, com quatro opções: espaçamento, espessura, escuridão e a cor da linha.
O mod pronto está em `mods/examples/scanlines`.

## 1. A pasta

Crie uma pasta `scanlines` dentro da sua pasta de mods com este `mod.json`:

```json
{
  "name": "Scanlines",
  "version": "1.0.0",
  "api": 1,
  "kind": "shader",
  "entry": "scanlines.glsl"
}
```

## 2. A função de efeito

Um mod de shader define uma função, `vec4 effect(vec2 pos)`. O David a chama para cada pixel da seleção (ou da camada inteira).
`pos` é a posição do pixel na imagem, e `pixel(pos)` lê a imagem ali. As cores são RGBA diretas, de 0 a 1.

```glsl
vec4 effect(vec2 pos) {
  return pixel(pos);
}
```

Esse efeito ainda não faz nada. Salve como `scanlines.glsl`, ative o mod em Configurações > Mods, e ele aparece em **Efeitos > Mods**.

## 3. Nome e opções

Linhas de comentário que começam com `// @` descrevem o efeito. Cada `@param` vira um uniform com o mesmo nome e um controle na janela:

```glsl
// @name Scanlines
// @id scanlines
// @category Effects/Stylize
// @param spacing int 2..16 = 2 "Spacing"
// @param thickness int 1..8 = 1 "Thickness"
// @param darkness float 0..1 = 0.5 "Darkness"
// @param tint color = #000000FF "Line color"
```

| Tipo | Linha | Uniform |
|---|---|---|
| float | `// @param id float min..max = default "Label"` | `float` |
| int | `// @param id int min..max = default "Label"` | `int` |
| bool | `// @param id bool = true "Label"` | `bool` |
| escolha | `// @param id choice One\|Two\|Three = 0 "Label"` | `int` (o índice) |
| cor | `// @param id color = #RRGGBBAA "Label"` | `vec4` |

`@id` é o nome que a CLI usa (`--effect scanlines:spacing=4`); sem ele, usa-se o nome da pasta. `@category` o coloca nos menus: `Effects/Stylize` é **Efeitos > Estilizar**, `Adjustments` é o menu Ajustes. A janela atrás do diálogo não é escurecida, para que a prévia ao vivo possa ser avaliada; adicione `// @dim` para escurecê-la como os outros diálogos.

## 4. As linhas

```glsl
vec4 effect(vec2 pos) {
  vec4 c = pixel(pos);
  bool line = mod(pos.y, float(spacing)) < float(thickness);
  if(line) c.rgb = mix(c.rgb, tint.rgb, darkness * tint.a);
  return c;
}
```

Salve. O mod recarrega, e **Efeitos > Estilizar > Scanlines** abre a janela com prévia ao vivo. OK vira um único passo de desfazer; Ctrl+F repete o efeito.

![A janela do Scanlines sobre sua prévia ao vivo](img/scanlines.png)

## O que mais

- Os uniforms `uSize` (o tamanho da área) e `uOrigin` (seu canto superior esquerdo na imagem) estão disponíveis se você precisar.
- `pixel()` limita nas bordas da área. A área é a caixa delimitadora da seleção.
- Um erro de GLSL aparece em Configurações > Mods com o número da linha, contado a partir do topo do seu arquivo.
- A CLI também roda mods de shader; ela abre uma janela oculta para a GPU.
