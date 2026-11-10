# game_engine

Engine de jogos multiplataforma escrita em C, com jogos escritos em **V** através
do módulo **EGN** (`egn/`).

## 🚧 Em desenvolvimento

- **Etapa 1 (concluída):** renderização 3D com Vulkan, texturas, câmera
  perspectiva/ortográfica e primitivas.
- **Sistema de cena (concluído):** hierarquia, transforms acumulados,
  quaternion + helpers Euler, layers, múltiplas câmeras com viewport e clear
  por câmera, e componentes `Camera`, `Model`, `Sprite`, `Animation`, `Light`,
  `Rigidbody`, `Collider` e `Particles`.
- **Etapa 2/3 (concluída):** carregamento de `.obj` multi-material (submeshes +
  `map_Kd`) e `.glb`/glTF binário com skeleton, skinning GPU e clips de
  animação.
- **Iluminação:** sol direcional com shadow map, luzes pontuais e luz ambiente.
- **Física:** Jolt Physics via JoltC: rigidbodies 2D/3D, colisores box, esfera
  e malha (inclusive malha dinâmica), com timestep fixo.
- **Partículas:** simulação em compute shader na GPU e renderização de
  billboards com textura, cor/tamanho ao longo da vida, gravidade e drag.
- **Rede:** TCP e UDP, servidores, clientes e host (servidor + cliente local),
  com fila de eventos e mensagens com tamanho prefixado no TCP.
- **Demo:** personagem Fox animado, chão, física, partículas, luzes, câmera FPS
  com pointer lock real no Wayland e captura de tela.
- **Web:** o mesmo jogo V compilado para WASM, com renderizador WebGPU
  web com física, OBJ/GLB, luzes, sombras e câmera FPS.

## Arquitetura

```text
engine/   Núcleo da engine em C, compilado como output/linux/libengine.so
          (plataforma, janela, Vulkan, inputs, eventos, memória, log, math,
           cena/objetos/componentes)

egn/      Módulo EGN em V: única ponte entre o jogo e a engine.
          O jogo importa `egn`; não conhece Wayland, Vulkan, etc.

game/     O jogo, escrito em V (game/main.v) e seus assets.
          game/v.mod: projeto V do jogo (nome, versão, ...).

third_part/  Dependências de terceiros:
             stb_image.h / stb_image_write.h (decodificação/gravação de imagens)
             v/ (compilador V clonado localmente; não versionado)

output/linux/  Saída Linux: game, libengine.so, shaders compilados e assets.
output/web/    Saída web: wasm, SDK JS, renderer WebGPU, demo e assets.
build/    Objetos intermediários da engine.
```

A engine é independente de plataforma: todo o código específico de sistema
fica em `engine/platform/<os>` e `engine/renderer/vulkan`. O jogo não contém
código de plataforma — ele só usa o módulo `egn`.

A engine **não bloqueia** o jogo com um loop interno. O jogo é dono do loop e
chama `app.frame()` enquanto `app.should_run()` for verdadeiro. A cena é
renderizada automaticamente a cada `frame()`, percorrendo as câmeras e os
componentes.

A swapchain usa `VK_PRESENT_MODE_FIFO_KHR` (vsync) por padrão, evitando que o
jogo rode a milhares de FPS e sobrecarregue a GPU/desktop.

## Dependências

Linux/Wayland:

```sh
sudo apt install clang g++ make libwayland-dev libvulkan-dev glslc
```

Opcional (habilita a camada de validação do Vulkan em desenvolvimento):

```sh
sudo apt install vulkan-validationlayers
```

A física usa JoltC/Jolt (MIT). O Makefile baixa e compila as dependências na
primeira vez:

```sh
make jolt
```

É necessário CMake. O Makefile procura `third_part/cmake` (instalado pelo
próprio projeto), depois `cmake` do `PATH`. Os protocolos Wayland de pointer
lock (`pointer-constraints` e `relative-pointer`) já estão vendorizados em
`engine/platform/linux/protocols`; não é preciso instalar `wayland-protocols`.

Compilador V (0.5+): <https://vlang.io>

O Makefile usa, nesta ordem, `third_part/v/v` (quando existir) ou `v` do
`PATH`. Para clonar e compilar o V dentro do repositório:

```sh
make v
```

Se o compilador estiver em outro lugar:

```sh
make V=/caminho/para/v
```

## Build e execução

O projeto tem um único `Makefile` na raiz, com detecção automática de
plataforma e alvos separados:

```sh
make            # engine + jogo (padrão; equivale a make all)
make all        # engine + jogo
make engine     # somente a engine (libengine.so + shaders)
make game       # somente o jogo (requer a engine já compilada)
make build      # alias de make all
make run        # compila tudo e executa o jogo
make clean      # remove output/ e build/
make v          # clona/compila o V em third_part/v
make help       # lista alvos e variáveis

make PLATFORM=linux   # força a plataforma
make CC=clang          # força o compilador C
make V=/caminho/para/v # força o compilador V
```

`make game` compila apenas o jogo (não recompila a engine), mas exige que
`output/linux/libengine.so` já exista; use `make engine` antes ou simplesmente `make`.
A plataforma é detectada automaticamente (`linux`, `macos`, ...). Novos
sistemas operacionais podem ser adicionados no bloco de plataforma do
`Makefile`.

Os assets de `game/assets/` são copiados para `output/linux/assets/` automaticamente.
Os shaders GLSL em `engine/shaders/` são compilados para SPIR-V em
`output/linux/shaders/`.

## Jogo mínimo em V

```v
module main

import egn { Settings, create }

fn main() {
    mut app := create(Settings{
        title:  'Meu jogo'
        width:  960
        height: 540
    })!
    defer {
        app.destroy()
    }

    for app.should_run() {
        app.frame()
    }
}
```

Para compilar um jogo que esteja dentro do repositório:

```sh
v -o output/linux/meu_jogo meu_jogo/
```

O `game/v.mod` define o projeto V do jogo. O Makefile passa
`-path '<raiz do repositório>|@vlib|@vmodules'` para que `import egn` encontre
a pasta `egn/` e a biblioteca padrão continue disponível. Para compilar
manualmente:

```sh
v -path '/caminho/para/game_engine|@vlib|@vmodules' -o output/linux/game game
```

Se o jogo ficar fora do repositório, adicione a raiz do repositório ao `-path`
do V ou copie o módulo `egn/` para junto do jogo.

## Cena, objetos e componentes

A cena é o objeto raiz (id 1). `app.scene()` devolve esse objeto; todos os
objetos são criados como filhos. As transformações dos pais são acumuladas nos
filhos (posição, rotação e escala), como na Unity.

Cada objeto tem `position`, `rotation` (quaternion), `scale`, `layer` (0..31),
`active`, pai/filhos e componentes.

```v
module main

import egn {
    Camera
    Model
    Settings
    Viewport
    create
    rgb
    rgba
    vec3
}

fn main() {
    mut app := create(Settings{ title: 'Cena', width: 1100, height: 620 })!
    defer {
        app.destroy()
    }

    scene := app.scene()

    // Um pivô com um filho: o filho herda a transformação do pai.
    pivot := scene.create_object('Pivot')
    pivot.set_position(vec3(1, 0, 0))

    cube := pivot.create_child_euler_deg('Cube', vec3(2, 0, 0), vec3(0, 45, 0), vec3(1, 1, 1))
    cube.set_layer(u32(1))
    cube.add_component(Model{
        mesh:  app.mesh_cube()
        color: rgb(1.0, 0.42, 0.12)
    })

    // Split-screen: duas câmeras na mesma layer, cada uma em meia tela.
    left := scene.create_object('Left Camera')
    left.set_position(vec3(-2.2, 1.6, 5))
    left.look_at(vec3(0, 0, 0))
    left.add_component(Camera{
        viewport:   Viewport{ x: 0.0, y: 0.0, width: 0.5, height: 1.0 }
        layer_mask: u32(1 << 1)
        clear_color: rgba(0.02, 0.04, 0.12, 1.0)
    })

    right := scene.create_object('Right Camera')
    right.set_position(vec3(2.2, 1.6, 5))
    right.look_at(vec3(0, 0, 0))
    right.add_component(Camera{
        viewport:   Viewport{ x: 0.5, y: 0.0, width: 0.5, height: 1.0 }
        layer_mask: u32(1 << 1)
        clear_color: rgba(0.08, 0.02, 0.10, 1.0)
    })

    for app.should_run() {
        cube.rotate_euler_deg(vec3(0, 1, 0))
        app.frame()
    }
}
```

O jogo de demonstração em [`game/main.v`](game/main.v) mostra:

- hierarquia com pivô + cubo filho;
- câmera perspectiva à esquerda e ortográfica à direita;
- os mesmos objetos na layer 1 visíveis nas duas metades;
- sprite exclusivo da layer 2 (só a câmera esquerda) e da layer 3 (só a
  direita);
- clear color/depth independente por câmera;
- **F12** salva um screenshot e **Esc** sai.

## Componentes

### Camera

| Campo | Descrição |
| --- | --- |
| `projection` | `.perspective` ou `.orthographic` |
| `fov` | FOV vertical em graus (perspectiva) |
| `ortho_size` | Altura visível em unidades de mundo (ortográfica) |
| `near`, `far` | Planos de recorte |
| `viewport` | `Viewport{ x, y, width, height }` normalizado (0..1) |
| `layer_mask` | Bitmask dos layers renderizados |
| `clear_color` | Cor de fundo da viewport |
| `clear_color_enabled` | Limpa a cor da viewport |
| `clear_depth_enabled` | Limpa o depth da viewport |
| `order` | Ordem de renderização entre câmeras |
| `enabled` | Ativa/desativa a câmera |

A câmera usa a transformação do objeto dono (posição + rotação), então
`object.look_at(target)` orienta a câmera. O aspect ratio é calculado pela área
da viewport.

### Model

Renderiza um mesh 3D com cor sólida ou textura. Por enquanto a engine fornece
os meshes `cube` e `sphere`; o carregamento de `.obj` entra na Etapa 2.

```v
obj.add_component(Model{
    mesh:    app.mesh_cube()
    texture: checker // opcional; vazio = branco
    color:   rgb(1.0, 0.42, 0.12)
})
```

### Sprite

Renderiza um quad texturizado em unidades de mundo no plano XY, virado para +Z.
O tamanho é multiplicado pela escala do objeto.

```v
obj.add_component(Sprite{
    texture: checker
    size:    vec2(1.0, 1.0)
    pivot:   vec2(0.5, 0.5)
    color:   rgb(1, 1, 1)
})
```

### Acessando e alterando componentes

`add_component` devolve um handle; `get_component[T]()` busca o primeiro
componente do tipo no objeto:

```v
mut cam := camera_object.get_component[Camera]() or {
    panic('camera component not found')
}
cam.set_fov(50)
cam.set_layer_mask(u32((1 << 1) | (1 << 2)))
```

Os métodos `set_*` atualizam a engine imediatamente. Se você alterar campos
diretamente, chame `apply()` para sincronizar:

```v
cam.fov = 50
cam.apply()
```

`component.enabled` e `object.set_active(false)` desativam o componente/objeto;
desativar um objeto também desativa seus filhos.

## Rotação (quaternion + Euler)

A rotação é armazenada como quaternion. Para conveniência há helpers Euler:

```v
obj.set_euler(vec3(0, 1.0, 0))       // radianos
obj.set_euler_deg(vec3(0, 45, 0))    // graus
obj.rotate_euler(vec3(0, 0.1, 0))    // delta local, radianos
obj.rotate_euler_deg(vec3(0, 5, 0))  // delta local, graus
obj.set_rotation_quat(quat_from_euler_deg(vec3(0, 45, 0)))
```

## API do SDK (resumo)

| Área | API |
| --- | --- |
| Ciclo de vida | `egn.create(Settings) !Engine`, `frame()`, `should_run()`, `quit()`, `destroy()` |
| Tempo | `delta_time()`, `elapsed_time()` |
| Janela | `window_size()` |
| Cena | `scene()`, `Object.create_object()`, `create_child()`, `create_child_euler_deg()` |
| Objetos | `set_position`, `position`, `set_rotation_quat`, `rotation`, `set_euler(_deg)`, `rotate_euler(_deg)`, `look_at`, `set_scale`, `scale`, `set_layer`, `layer`, `set_active`, `active`, `set_parent`, `parent`, `child_count`, `child_at`, `destroy` |
| Componentes | `add_component(Camera/Model/Sprite)`, `get_component[T]()`, `set_enabled`, `apply`, `destroy` |
| Câmera | `set_projection`, `set_fov`, `set_ortho_size`, `set_clip_planes`, `set_viewport`, `set_layer_mask`, `set_clear_color`, `set_order` |
| Model/Sprite | `set_mesh`, `set_texture`, `set_color`, `set_size`, `set_pivot` |
| Recursos | `load_texture`, `release_texture`, `mesh_cube`, `mesh_sphere`, `screenshot` |
| Teclado | `key_down(Key)`, `key_up(Key)`, `key_pressed(Key)`, `key_released(Key)` |
| Mouse | `mouse_down(MouseButton)`, `mouse_up`, `mouse_pressed`, `mouse_released`, `mouse_position()`, `mouse_previous_position()`, `mouse_wheel()`, `mouse_previous_wheel()` |
| Eventos | `on_event(EventCode, callback)`, `off_event`, `fire_event` |
| Log | `egn.log_info`, `log_warn`, `log_error`, `log_debug`, `log_trace`, `log_fatal` |
| Math | `Vec2`, `Vec3`, `Vec4`, `Quat`, `Rect`, `Viewport`, `Color` |

## Estrutura do módulo

```text
egn/
  v.mod         declaração do módulo `egn`
  bindings.v    declarações C (ABI da engine) e flags de link
  engine.v      Engine, Settings, loop, input e app.scene()
  scene.v       Object, Camera, Model, Sprite, hierarquia e componentes
  render.v      Color, Viewport, Texture, Mesh, Projection e screenshot
  events.v      EventCode, EventContext, EventCallback
  keys.v        enums Key e MouseButton
  log.v         LogLevel e helpers de log
  math.v        Vec2, Vec3, Vec4, Quat, Rect e helpers
  README.md     guia para estender o módulo
```

Detalhes para adicionar novas funções da engine ao módulo estão em
[`egn/README.md`](egn/README.md).


## Demo e controles

O jogo em `game/main.v` demonstra a engine completa:

- chão texturizado com colisão estática;
- Fox do Khronos com skeleton e animação `Walk`;
- humanoide de Quaternius com animação esquelética `Idle` em loop;
- caixas, esfera, corpo 2D e um OBJ com colisor de malha dinâmico;
- sol com sombras, luzes pontuais, fogo e cristal com partículas na GPU;
- câmera FPS com mouse travado de verdade no Wayland;
- contador de FPS renderizado pela engine no canto superior esquerdo, com VSync desativado por padrão.

Controles:

| Tecla | Ação |
| --- | --- |
| `WASD` | W/S na direção da câmera, A/D strafe |
| `Space` | pular |
| `Shift` | correr |
| mouse | olhar (clique captura o ponteiro) |
| botão esquerdo | pegar / soltar corpo dinâmico |
| botão direito | arremessar corpo segurado |
| `Esc` | encerra o jogo |
| `F1` | alterna vsync |
| `F2` | alterna limite de FPS (off/60/120/144) |
| `F12` | salva `screenshot.png` |

A camada de rede continua disponível no SDK (`egn/network.v`), mas o demo
principal não abre sockets.

## Assets de terceiros

`game/assets/models/fox.glb` é o modelo Fox do Khronos glTF Sample Models:
- low poly fox por PixelMannen (CC0);
- rigging/animação por @tomkranis (CC BY 4.0).

Consulte `game/assets/models/LICENSE_FOX.md` e `game/assets/models/LICENSE_HUMANOID.md`.


## Build web (WASM + WebGPU)

O build Web compila o mesmo `game/main.v` usado no Linux para WebAssembly,
junto ao núcleo C e à física Jolt. O JavaScript em `engine/platform/web/` e
`engine/renderer/webgpu/` é parte interna do backend gráfico e da integração
com o navegador; a página apenas cria o canvas e inicia a engine.

```sh
make emsdk    # instala o Emscripten SDK em third_part/
make naga     # instala o validador WGSL
make web      # compila o jogo V e a engine para output/web/
make run-web  # abre a demo no Chromium com WebGPU
```

WebGPU requer um navegador compatível e um contexto seguro (localhost/HTTPS).
O VSync e a apresentação no navegador são controlados pelo próprio navegador.
Consulte `web/README.md` para detalhes da arquitetura e das limitações.

### Configuração gráfica (primeira etapa)

A API C permite configurar as sombras antes de `engine_create_instance` e
alterá-las entre quadros. A estrutura completa é validada e aplicada de uma vez;
valores inválidos são rejeitados sem alterar a configuração anterior.

```c
#include "renderer/renderer.h"

RendererGraphicsSettings graphics = renderer_graphics_settings_default();
graphics.shadows_enabled = true;
graphics.shadow_quality = RENDERER_QUALITY_HIGH;
renderer_set_graphics_settings(&graphics);
```

Os níveis low, medium e high usam respectivamente 1, 9 e 25 amostras de
filtragem; o mapa continua com resolução fixa de 2048 pixels. Desativar sombras
elimina o passe de sombras. A configuração padrão mantém o comportamento anterior
(ativo, medium). A API também está disponível no WebGPU e no SDK V. Iluminação global, HDR, tone mapping,
color grading, dispersão atmosférica, oclusão de ambiente, bloom e exposição
automática ainda não foram implementados por esta etapa.

No SDK V, `Settings.graphics` configura a criação e `egn.set_graphics_settings`
aplica uma estrutura completa durante a execução. `egn.graphics_settings()`
retorna os valores atuais. Ambas as chamadas afetam o renderizador global.

```v
mut graphics := egn.GraphicsSettings{ shadow_quality: .high }
mut app := egn.create(egn.Settings{ graphics: graphics })!
graphics.shadows_enabled = false
egn.set_graphics_settings(graphics)
```

### Etapas da atualização gráfica

1. **Configuração em lote e sombras:** concluída para Vulkan e WebGPU; a
   qualidade controla a filtragem, não a resolução do mapa.
2. **Materiais e iluminação PBR:** em andamento. O WebGPU usa metallic e
   roughness na iluminação direta; falta o suporte Vulkan e validação visual.
3. **HDR e pós-processamento:** buffers HDR, tone mapping, color grading e
   exposição automática.
4. **Efeitos de espaço de tela:** ambient occlusion e bloom, com controles de
   qualidade e recursos dimensionados por preset.
5. **Ambiente e iluminação indireta:** atmospheric scattering e melhoria da
   iluminação global; a técnica dependerá das capacidades dos backends.
6. **Integração e validação:** demonstrar presets, trocas em runtime e custos
   de desempenho; verificar o comportamento visual nos dois backends.

As etapas futuras ainda não estão implementadas. Ao adicionar opções que
exijam novos recursos GPU, a configuração será aplicada em lote em um limite
seguro entre quadros.
