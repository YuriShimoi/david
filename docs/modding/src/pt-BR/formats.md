# Padrões, temas e formatos de arquivo

Isto não é código, mas também são formas de estender o David.

## Padrões de pontilhado

**Efeitos > Cor > Pontilhado** com o método **Padrão** usa uma matriz de limiares. O David traz Bayer 2×2, 4×4, 8×8 e ruído azul, e carrega outros da pasta `patterns` ao lado do programa e da sua pasta de configuração. Um padrão é referenciado pelo seu id, inclusive pela CLI: `--effect dither:method=Pattern,pattern=my-pattern`.

| Arquivo | Id | Formato |
|---|---|---|
| `.png` | o nome do arquivo | Uma imagem em tons de cinza; mais escuro significa limiar menor. Os níveis de cinza distintos são ordenados, então qualquer faixa serve (0–3, 0–255, ...). |
| `.json` | o nome do arquivo | `[[0, 8, 2, 10], [12, 4, 14, 6], ...]`, ou `{ "matrix": [...], "max": 15 }` quando os valores não chegam ao máximo. |
| `.csv` | o nome do arquivo | Uma linha por linha do arquivo, com valores separados por vírgulas. |
| `.aseprite-extension` | o `id` de cada matriz | O zip de extensão do Aseprite: um `package.json` com `contributes.ditheringMatrices` (`id`, `name`, `path`) ao lado dos arquivos de matriz. O mesmo pacote funciona no Aseprite e no David. |

Um valor v em uma matriz cujo maior valor é max vira o limiar (v + 0.5) / (max + 1).

## Temas

Um tema é um arquivo JSON na pasta `themes` (nativos) ou na pasta `themes` da sua configuração (os seus). Configurações > Aparência cria uma cópia para você editar, e o David a recarrega quando o arquivo muda.

```json
{
  "name": "Dev",
  "colors": { "bg": "#1A1A1A", "surface": "#212121", "accent": "#8BE9FD", "...": "..." },
  "shape": { "radius": 4, "borderWidth": 0, "spacing": 6, "fontSize": 15, "font": "" }
}
```

| Token | Usado para |
|---|---|
| `bg`, `surface`, `raised` | Fundo da janela, painéis, campos e botões |
| `line`, `hover`, `pressed` | Divisores e preenchimentos neutros, e seus estados |
| `text`, `textMuted` | Texto e texto secundário |
| `accent`, `accentHover`, `onAccent` | A única cor de destaque, seu estado hover e o texto sobre ela |
| `good`, `warn`, `bad` | Cores de status |
| `canvasBg`, `checkerA`, `checkerB` | Ao redor da imagem e o xadrez de transparência |
| `selectionAnts`, `nub` | Formigas marchantes e alças na tela |
| `radius`, `borderWidth`, `spacing`, `fontSize`, `font` | Raio dos cantos, largura da borda, espaçamento entre itens, tamanho da fonte da interface e arquivo de fonte |

## O arquivo .david

Um zip: `doc.json`, `layers/0.png`, `layers/1.png`, ... (a de baixo primeiro) e `merged.png` (a imagem achatada, para pré-visualizações).

```json
{
  "app": "david", "version": 1, "width": 64, "height": 64, "dpi": 96, "active": 1,
  "palette": ["1a1c2c", "5d275d"],
  "layers": [
    { "name": "Background", "visible": true, "opacity": 255, "blend": "normal", "file": "layers/0.png" }
  ]
}
```

O David também lê e escreve OpenRaster (`.ora`), que o Krita, o GIMP e o MyPaint abrem. Os modos de mesclagem que o OpenRaster não tem são guardados como `david:<mode>`, que os outros programas leem como normal.
