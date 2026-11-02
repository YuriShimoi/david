# Primeiros passos

O David tem três tipos de mod. Escolha o mais simples que dê conta do trabalho:

| Nível | O que é | Bom para |
|---|---|---|
| Shader | Um arquivo `.glsl` | Efeitos por pixel que rodam na GPU: mudanças de cor, padrões, distorções |
| Lua | Um ou mais arquivos `.lua` | Efeitos, comandos que editam camadas, ferramentas de pintura |
| Native | Uma DLL ou `.so` compilada a partir de C ou C++ | Efeitos pesados e novos formatos de arquivo |

## Onde ficam os mods

Todo mod é uma pasta com um `mod.json` dentro. O David procura em dois lugares:

- a pasta `mods` ao lado do programa (mods que acompanham o David, e `mods/examples/`);
- a pasta `mods` dentro da sua pasta de configuração: `%APPDATA%\David\mods` no Windows, `~/.config/david/mods` no Linux. **Mods > Abrir pasta de mods** abre essa pasta.

Um mod na sua pasta de configuração substitui um mod embutido com o mesmo nome de pasta.

## mod.json

```json
{
  "name": "Scanlines",
  "version": "1.0.0",
  "api": 1,
  "kind": "shader",
  "entry": "scanlines.glsl",
  "description": "Darkens every other group of rows."
}
```

| Campo | Significado |
|---|---|
| `name` | Mostrado em Configurações > Mods. |
| `version` | A versão do seu mod, qualquer texto. |
| `api` | A versão da API do David de que o mod precisa. Este David tem a versão 1; um número maior impede o carregamento. |
| `kind` | `shader`, `lua` ou `native`. |
| `entry` | O arquivo que o David carrega: o arquivo `.glsl`, o arquivo `.lua` principal, ou o nome da biblioteca sem extensão (`qlike` carrega `qlike.dll` ou `qlike.so` / `libqlike.so`). |
| `description` | Uma linha, mostrada embaixo do nome. |
| `permissions` | Lista opcional, mostrada ao usuário. A versão 1 não concede nada extra: mods Lua não acessam arquivos nem a rede de qualquer forma. |

## Ativando mods e vendo erros

**Configurações > Mods** (ou **Mods > Gerenciar mods**) lista todos os mods com uma chave. Seus próprios mods começam ativados; os exemplos começam desativados.
Um mod que falha ao carregar mostra o erro em vermelho embaixo do nome: um erro de JSON, um erro de sintaxe Lua com a linha, ou um erro de compilação GLSL.
O que `david.log` e `print` escrevem aparece em **Log**.

![Configurações > Mods com os mods de exemplo e um com defeito](img/mods.png)

## O ciclo de edição

Mods de shader e de Lua recarregam sozinhos: deixe o David aberto, salve o arquivo e em até um segundo a nova versão está nos menus.
Se a janela de um efeito estiver aberta, o recarregamento espera você fechá-la. Mods nativos e novas ferramentas Lua precisam de reinício.

Os mesmos mods rodam pela linha de comando, o que é útil para testes rápidos:

```
david-cli --mod scanlines in.png --effect scanlines:spacing=4 -o out.png
david-cli --list-effects
```

## Próximos passos

- [Efeito de shader: Scanlines](tutorial-shader.html)
- [Comando Lua: Palette Swap](tutorial-lua-command.html)
- [Ferramenta Lua: Checker Brush](tutorial-lua-tool.html)
- [Janelas Lua: Canvas Info](tutorial-lua-window.html)
- [Plugin nativo: um tipo de arquivo tipo QOI](tutorial-native.html)
- [Referência da API](reference.html)
