# Conceitos

## Pixels e coordenadas

- As coordenadas são pixels da imagem a partir do canto superior esquerdo, começando em 0. `x` cresce para a direita, `y` para baixo.
- Uma cor é um inteiro de 32 bits: RGBA direto (não pré-multiplicado), com o vermelho no byte baixo. No Lua, use `david.rgba` e `david.channels` em vez de fazer a conta de bits.
- As camadas são numeradas a partir de 1 (a camada de baixo) no Lua, como é de costume no Lua.

## Tiles

Uma imagem é armazenada em tiles de 256 × 256 pixels, e um tile vazio não ocupa memória. Por isso uma camada nova de 8000 × 8000 não custa nada até você pintar nela. Para os mods isso é invisível: `get` e `set` funcionam em qualquer lugar da imagem.

## A seleção

A seleção é uma máscara com um valor de 0 a 255 por pixel, não uma forma: bordas suaves (da varinha mágica ou de um laço com suavização) são pixels parcialmente selecionados.

- Os efeitos só alteram os pixels dentro da seleção, misturados pela máscara: um pixel selecionado em 128 recebe metade do efeito. Você não precisa fazer nada para isso.
- A imagem de um efeito é a caixa delimitadora da seleção mais a margem do efeito. Fora da caixa delimitadora nada muda.
- Os comandos enxergam a máscara por `doc:selected(x, y)` e `doc:selectionBounds()`.

## Transações de desfazer

Cada execução de efeito, de comando ou traço de ferramenta é uma etapa do Histórico. O David registra os tiles que uma etapa toca (antes e depois), então a memória de desfazer continua pequena mesmo em imagens grandes.
Dentro de um comando Lua, toda alteração entra na mesma etapa, em ordem, e um erro de Lua desfaz tudo o que o comando fez.

## Espaços de cor

As cores são sRGB de 8 bits. Algumas operações funcionam em outros espaços:

- **Luz linear** para reamostragem (Imagem > Redimensionar imagem, com a opção de gama ativada).
- **OKLab**, um espaço perceptual, para a tolerância da varinha mágica e do balde de tinta, gradientes, correspondência de paleta em Pontilhado e Matiz / saturação. Os mods podem fazer as próprias conversões; as fórmulas estão no [artigo sobre OKLab de Björn Ottosson](https://bottosson.github.io/posts/oklab/).

## O sandbox

Os mods Lua rodam apenas com `math`, `string`, `table`, `utf8` e as partes seguras da biblioteca base. Não há `os`, `io`, `package`, `require`, `load`, `loadfile` nem `dofile`, e bytecode compilado não pode ser carregado.
Um mod Lua só pode alterar a imagem que recebe. Cada chamada tem um limite de 10 segundos.

Os mods de shader só enxergam a área em que rodam. Os plugins nativos não têm sandbox (veja o [tutorial nativo](tutorial-native.html)).
