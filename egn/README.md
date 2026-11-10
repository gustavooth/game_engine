# Módulo EGN (SDK em V)

Este módulo (`egn`) é a única ponte entre os jogos escritos em V e a engine em
C (`libengine.so`). O jogo nunca inclui headers C nem chama símbolos da engine
diretamente.

## Como funciona

- `bindings.v` declara a ABI C da engine (`fn C.engine_*`, `fn C.inputs_*`,
  `fn C.events_*`, `fn C.renderer_*`, `fn C.egn_*`) e as flags de compilação:
  - `-I @VMODROOT/../engine`: headers da engine;
  - `-L @VMODROOT/../output/linux` + `-lengine`: link contra `libengine.so`;
  - `-Wl,-rpath,'$ORIGIN'`: o executável encontra a biblioteca ao lado dele.
- `engine.v` expõe `Engine`, `Settings`, loop, input e `app.scene()`.
- `scene.v` expõe o grafo de cena: `Object`, `Camera`, `Model`, `Sprite`,
  `Animation`, `Light`, `Rigidbody`, `Collider`, `Particles`, hierarquia,
  transforms e layers.
- `render.v` expõe `Projection`, `Viewport`, `Color`, `Texture`, `Mesh`,
  `load_texture`, `load_obj`, `load_glb`, `mesh_cube`, `mesh_sphere` e
  `screenshot`.
- `network.v` expõe `Server`, `Client` e `Host` para TCP/UDP não-bloqueante.
- `engine.v` expõe `Settings.vsync/target_fps`, `set_vsync`, `set_target_fps`,
  `set_pointer_lock`, `pointer_locked` e `mouse_delta`.
- `math.v` define `Vec2`, `Vec3`, `Vec4`, `Quat` e helpers de Euler.
- A engine roda um frame por chamada de `frame()`; o loop pertence ao jogo.

## Importando

É possível usar o módulo qualificado:

```v
import egn

fn main() {
    mut app := egn.create(egn.Settings{ title: 'Meu jogo' })!
    scene := app.scene()
    cube := scene.create_object('Cube')
    cube.add_component(egn.Model{
        mesh: app.mesh_cube()
        color: egn.rgb(1.0, 0.5, 0.1)
    })
}
```

Ou trazer os símbolos usados para o contexto do jogo com *selective imports*:

```v
import egn {
    Camera
    Color
    Model
    Object
    Projection
    Settings
    Sprite
    Texture
    Vec2
    Vec3
    Viewport
    create
    rgb
    rgba
    vec2
    vec3
}

fn main() {
    mut app := create(Settings{ title: 'Meu jogo' })!
    scene := app.scene()
    cube := scene.create_object('Cube')
    cube.set_position(vec3(0, 0, 0))
    cube.set_euler_deg(vec3(0, 45, 0))
    cube.set_layer(u32(1))
    cube.add_component(Model{
        mesh: app.mesh_cube()
        color: rgb(1.0, 0.5, 0.1)
    })

    cube_camera := scene.create_object('Camera')
    cube_camera.set_position(vec3(0, 1.5, 5))
    cube_camera.look_at(vec3(0, 0, 0))
    cube_camera.add_component(Camera{
        viewport: Viewport{ x: 0, y: 0, width: 1, height: 1 }
        layer_mask: u32(1 << 1)
    })

    for app.should_run() {
        app.frame()
    }
}
```

Assim `Vec3`, `vec3`, `Color`, `rgb`, `Camera`, `Texture`, `Viewport` etc. podem
ser usados sem o prefixo `egn.`.

## Cena, objetos e hierarquia

A cena é o objeto raiz (id 1). `app.scene()` devolve esse objeto; todos os
outros objetos são criados como filhos:

```v
scene := app.scene()
pivot := scene.create_object('Pivot')
pivot.set_position(vec3(1, 0, 0))

child := pivot.create_object('Child')
child.set_position(vec3(2, 0, 0)) // mundo: x = 3
```

Transformações dos pais são acumuladas nos filhos (posição, rotação e escala),
como na Unity. `create_child_euler_deg` cria um objeto já com posição, rotação
Euler em graus e escala.

Cada objeto tem:

- `position`, `rotation` (quaternion) e `scale`;
- `layer` (0..31);
- `active` (desativa o objeto e seus filhos);
- pai e lista de filhos;
- componentes.

Helpers de rotação:

```v
obj.set_euler(vec3(0, 1.0, 0))       // radianos
obj.set_euler_deg(vec3(0, 45, 0))    // graus
obj.rotate_euler(vec3(0, 0.1, 0))    // delta local, radianos
obj.rotate_euler_deg(vec3(0, 5, 0))  // delta local, graus
obj.look_at(vec3(0, 0, 0))
obj.set_rotation_quat(quat_from_euler_deg(vec3(0, 45, 0)))
```

## Componentes

### Camera

- Projeção `perspective` ou `orthographic`.
- `fov` (perspectiva) ou `ortho_size` (altura visível em unidades de mundo).
- `near`, `far`.
- `viewport` normalizado `(x, y, width, height)` em 0..1.
- `layer_mask`: bitmask dos layers que a câmera renderiza.
- `clear_color` e flags `clear_color_enabled` / `clear_depth_enabled`.
- `order`: ordem de renderização quando há várias câmeras.

Duas câmeras na mesma layer com viewports diferentes fazem split-screen:

```v
left := scene.create_object('Left Camera')
left.set_position(vec3(-2.2, 1.6, 5))
left.look_at(vec3(0, 0, 0))
left.add_component(Camera{
    viewport: Viewport{ x: 0.0, y: 0.0, width: 0.5, height: 1.0 }
    layer_mask: u32(1 << 1)
    clear_color: rgba(0.02, 0.04, 0.12, 1.0)
})

right := scene.create_object('Right Camera')
right.set_position(vec3(2.2, 1.6, 5))
right.look_at(vec3(0, 0, 0))
right.add_component(Camera{
    viewport: Viewport{ x: 0.5, y: 0.0, width: 0.5, height: 1.0 }
    layer_mask: u32(1 << 1)
    clear_color: rgba(0.08, 0.02, 0.10, 1.0)
})
```

O aspect ratio é calculado pela área da viewport. Cada câmera limpa sua área
com sua cor/depth, permitindo fundos diferentes no split-screen.

### Model

Renderiza um mesh 3D (cubo/esfera por enquanto; OBJ na próxima etapa) com cor
sólida ou textura:

```v
obj.add_component(Model{
    mesh: app.mesh_cube()
    texture: checker // opcional; 0/textura vazia = branco
    color: rgb(1.0, 0.42, 0.12)
})
```

### Sprite

Renderiza um quad texturizado em unidades de mundo no plano XY, virado para
+Z. O tamanho é multiplicado pela escala do objeto:

```v
obj.add_component(Sprite{
    texture: checker
    size: vec2(1.0, 1.0)
    pivot: vec2(0.5, 0.5)
    color: rgb(1, 1, 1)
})
```

### Acessando componentes

`add_component` devolve um handle do componente. Também é possível buscar o
primeiro componente de um tipo:

```v
mut cam := camera_object.get_component[Camera]() or {
    panic('camera component not found')
}
cam.set_fov(50)
cam.set_layer_mask(u32((1 << 1) | (1 << 2)))
```

Campos de `Camera`, `Model` e `Sprite` são mutáveis, mas os métodos `set_*`
atualizam a engine imediatamente. Se você alterar campos diretamente, chame
`apply()` para sincronizar:

```v
cam.fov = 50
cam.apply()
```

`component.enabled` e `object.set_active(false)` desativam o componente/objeto;
desativar um objeto também desativa seus filhos.

## Recursos

```v
tex := app.load_texture('assets/textures/checker.png')!
defer { app.release_texture(tex) }

cube_mesh := app.mesh_cube()
sphere_mesh := app.mesh_sphere()

if app.key_pressed(.f12) {
    app.screenshot('screenshot.png')
}
```

## Adicionando uma nova função da engine

1. Exporte a função em C com `EGNAPI` (ex.: `engine/core/engine.h`).
2. Declare-a em `egn/bindings.v` com os tipos equivalentes:

```v
fn C.engine_minha_funcao(instance_ptr voidptr, valor int) bool
```

3. Crie um wrapper tipado em `egn/engine.v` (ou no arquivo temático correto):

```v
pub fn (e &Engine) minha_funcao(valor int) bool {
    return C.engine_minha_funcao(e.handle, valor)
}
```

4. Compile e teste: `make`.

Regras de ABI:

- Use tipos primitivos de tamanho fixo (`int`, `u16`, `u64`, `f64`, `voidptr`)
  nas declarações C do SDK.
- Evite passar unions ou structs C por valor através da FFI. Prefira passar
  ponteiros ou usar funções-ponte que recebem palavras de 64 bits
  (`events_fire2` é um exemplo).
- A API de cena usa handles `voidptr` e funções com `f32`/`u32`/`i32`/`bool`.
  Os wrappers V convertem `Vec2`/`Vec3`/`Quat`/`Color` em parâmetros primitivos.
- Strings C (`const char *`) são `&char` no SDK. Mantenha a `string` V viva
  enquanto o lado C puder referenciá-la (veja o campo `title` em `Engine`).

## Eventos

O callback do jogo precisa ser uma função comum (não closure):

```v
import egn { EventContext }

fn on_resize(code u16, sender voidptr, data EventContext) bool {
    width := data.u16_at(0)
    height := data.u16_at(1)
    return false
}
```

`EventContext` é uma union C de 16 bytes exposta como `[16]u8` + acessores
tipados (`u16_at`, `i32_at`, `i64_at`, `f64_at`, `u8_at`). Para disparar eventos
com dados, use `app.fire_event(code, ctx)`.

## Portabilidade

As flags de link específicas de plataforma usam a sintaxe do V:

```v
#flag linux -Wl,-rpath,'$ORIGIN'
#flag windows -lengine
```

O código V do módulo `egn` é o mesmo em todas as plataformas. As implementações
de janela/superfície continuam em `engine/platform/<os>` e
`engine/renderer/vulkan/platform/`.


## Componentes novos

```v
// Personagem animado (.glb com skeleton + clips).
fox.add_component(Model{ mesh: app.load_glb('assets/models/fox.glb')! })
fox.add_component(Animation{ clip: 1, speed: 1.2, loop: true })

// Física Jolt.
box.add_component(Rigidbody{ motion_type: .dynamic, mass: 1.0 })
box.add_component(Collider{ shape: .box, size: vec3(1, 1, 1) })

// Colisor de malha (inclusive em corpo dinâmico).
obj.add_component(Collider{ shape: .mesh, mesh: app.load_obj('assets/models/box.obj')! })

// Partículas na GPU.
node.add_component(Particles{
    max_particles: 512
    texture: particle_texture
    spawn_rate: 80
    lifetime: 1.4
    speed: 1.5
    gravity: 1.2
    color_start: rgba(1.0, 0.8, 0.2, 1.0)
    color_end: rgba(1.0, 0.1, 0.0, 0.0)
})
```

Use `motion_type: .kinematic` para objetos controlados pelo jogo, `.static`
para chão/paredes e `is_2d: true` no `Rigidbody` para restringir o movimento ao
plano XY. Colisores `box`/`sphere` funcionam em 2D e 3D; `mesh` usa os dados
triangulados do asset carregado.

## Rede

```v
import egn { server_create, client_create, log_info }

mut server := server_create(9000, false)! // false = TCP, true = UDP
mut client := client_create('127.0.0.1', 9000, false)!

for app.should_run() {
    server.poll()
    client.poll()
    for {
        event := server.poll_event() or { break }
        if event.event_type == .message {
            log_info('recebi: ${event.message()}')
            server.send_text(event.client_id, 'pong')
        }
    }
    app.frame()
}
```

Mensagens TCP têm tamanho prefixado automaticamente; UDP preserva datagramas.
`Host` combina um servidor e um cliente conectado a `127.0.0.1` no mesmo
processo, útil para testes e modos single-player com servidor local.
