# EGN Web (WASM + WebGPU)

O build Web executa o mesmo código V de `game/` e o mesmo SDK V `egn/` usados
no Linux. O compilador V gera C para Emscripten, que liga o jogo ao núcleo C da
engine e à física Jolt em um único módulo WebAssembly. Não existe uma cena ou
SDK de jogo em JavaScript.

O código em `engine/platform/web/host.js` inicializa o backend do navegador,
o canvas, os eventos de entrada e o renderizador interno WebGPU. Os módulos em
`engine/renderer/webgpu/` implementam esse backend gráfico. O arquivo
`web/demo/index.html` apenas fornece o canvas e chama a inicialização da engine.
Os assets ficam pré-carregados no sistema de arquivos do WebAssembly e também
são servidos ao backend WebGPU para carregamento de modelos e texturas.

```sh
make emsdk
make naga
make web
make run-web
```

O build usa `-gc none` no V, pois o coletor Boehm não está integrado ao alvo
Emscripten. Jogos que façam alocações repetidas devem gerenciar sua vida útil;
essa configuração não oferece coleta automática de lixo.

O build não exige Node.js ou npm. Para desenvolvimento, é possível servir
`output/web` via HTTP em localhost; WebGPU também exige um navegador compatível.
O backend usa Asyncify para suspender o loop V entre quadros e para carregar
assets sem bloquear o navegador. VSync e apresentação de quadros no navegador
são determinados pelo navegador e podem diferir do Vulkan nativo. F12 solicita
um download da imagem do canvas.

Os shaders WGSL são validados pelo Naga. O código V e as APIs de cena, física,
texto e jogo são compartilhados; os backends Vulkan e WebGPU permanecem
implementações gráficas diferentes, mas recebem o mesmo lote de
`GraphicsSettings`.

O backend WebGPU implementa o mesmo pipeline conceitual do Vulkan: alvo HDR,
tone mapping/color grading, LUT 3D `.cube`, CSM com textura array, PBR com
mapas metallic-roughness/normal/oclusão/emissive, céu atmosférico, SSAO+SSGI,
bloom e auto exposure por histograma em compute shader. A configuração é
aplicada no início do frame pelo mesmo `set_graphics_settings`; o host envia o
lote completo como JSON somente quando ele muda. A demo usa `ultra` e os
atalhos `F3`..`F11`/`L` descritos no README principal.

O ambiente de desenvolvimento pode não expor um adaptador WebGPU mesmo com
`navigator.gpu` presente; nesse caso a demo mostra um erro controlado. O build
e a validação WGSL continuam funcionando sem GPU.
