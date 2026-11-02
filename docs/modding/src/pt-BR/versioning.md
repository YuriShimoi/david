# Versionamento

## A versão da API

O David tem uma única versão de API para os três níveis. Ela é a **1**.

- O `"api"` do `mod.json` indica a versão de que um mod precisa. O David recusa mods que precisam de uma versão mais nova que a dele, com um erro claro.
- Mods Lua podem ler `david.apiVersion`; plugins nativos recebem `host->api_version`.

## A política

- Novas funções, campos e parâmetros podem chegar na mesma versão. Os mods devem ignorar o que não conhecem.
- Remover ou alterar qualquer coisa aumenta a versão. Um recurso obsoleto continua funcionando por pelo menos mais uma versão e aparece primeiro na lista abaixo.
- Os ids dos efeitos (`gaussian-blur`, `dither`, ...) e os ids de seus parâmetros fazem parte da API, porque scripts da CLI os usam.
- A ABI nativa só cresce no final de suas structs; um aumento de versão vem com um novo `david_plugin.h`.

## Mudanças

| Versão | Mudanças |
|---|---|
| 1 | Primeira versão: efeitos de shader com `@param`, a API Lua `david` (efeitos, comandos, ferramentas, documentos, camadas), `david_plugin.h` (efeitos, formatos de arquivo), CLI `--effect`, `--script`. |
| 1 (adicionado) | Quadros de animação: `doc:frameCount()`, `doc.activeFrame`, `doc.fps`, `doc:frameDuration()` / `setFrameDuration()`, `doc:addFrame()`, `doc:deleteFrame()`, `doc.tags`. As camadas leem e escrevem no quadro ativo, então scripts antigos funcionam em qualquer quadro que esteja visível. CLI `--frame`, `--all-frames`, `--fps`, `--tag`, `--scale`, `--loops`, `--sheet`, `--slice`. |
| 1 (adicionado) | As janelas de efeito não escurecem mais a janela atrás delas; um efeito pede o escurecimento com `dim = true` (Lua) ou `// @dim` (shader). |
| 1 (adicionado) | Janelas: `david.window{...}` (janelas de ferramenta que acoplam, e diálogos), os widgets de `ui`, `david.openWindow` / `closeWindow`. |

## Obsoleto

Nada ainda.
