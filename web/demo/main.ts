import type * as EGNT from '../sdk';

type EgnNamespace = typeof EGNT;

export async function startDemo(EGN: EgnNamespace, moduleFactory: EGNT.EgnModuleFactory): Promise<void> {
  const canvas = document.querySelector<HTMLCanvasElement>('#egn-canvas');
  const status = document.querySelector<HTMLElement>('#status');
  const hud = document.querySelector<HTMLElement>('#hud');
  if (!canvas || !status || !hud) {
    throw new Error('demo page is missing required elements');
  }

  status.textContent = 'Inicializando WebAssembly + WebGPU...';

  const engine = await EGN.Engine.create({
    canvas,
    moduleFactory,
    powerPreference: 'high-performance',
  });

  const scene = engine.scene;
  const existingCamera = scene.findCamera();
  const cameraObject = existingCamera ?? scene.createObject('Web Camera');
  cameraObject.setName('FPS Camera');
  cameraObject.setPosition(0, 1.7, 6.2);
  cameraObject.setEuler(-0.08, 0, 0);
  if (!existingCamera) {
    cameraObject.addCamera({
      fov: 65,
      near: 0.05,
      far: 300,
      layerMask: 0xffffffff,
      clearColor: [0.035, 0.055, 0.11, 1],
    });
  }
  engine.setAmbient(0.09, 0.10, 0.13, 1);

  const groundTexture = await engine.loadTexture('assets/textures/ground.png');
  const checkerTexture = await engine.loadTexture('assets/textures/checker.png');
  const crateMesh = await engine.loadOBJ('assets/models/box.obj');
  const fox = await engine.loadGLB('assets/models/fox.glb');

  const foxObject = scene.createObject('Fox');
  foxObject.setPosition(0, 0.03, 0);
  foxObject.setEulerDeg(0, 0, 0);
  foxObject.setScale(0.02, 0.02, 0.02);
  foxObject.addModel({ mesh: fox.mesh, texture: fox.texture, color: [1, 1, 1, 1] });

  const ground = scene.createObject('Ground');
  ground.setPosition(0, -0.05, 0);
  ground.setScale(30, 0.1, 30);
  ground.addModel({ mesh: engine.meshCube(), texture: groundTexture });
  ground.addCollider({ shape: 'box', size: [1, 1, 1] });

  for (let i = 0; i < 6; i++) {
    const x = -1.6 + i * 0.6;
    const height = 1.8 + i * 0.9;
    const crate = scene.createObject(`Crate ${i}`);
    crate.setPosition(x, height, 1.4);
    crate.setEulerDeg(i * 11, i * 23, 0);
    crate.setScale(0.5, 0.5, 0.5);
    crate.addModel({
      mesh: crateMesh,
      texture: checkerTexture,
      color: [1, 1, 1, 1],
    });
    crate.addRigidbody({ motion: 'dynamic', mass: 1, friction: 0.55 });
    if (i === 5) {
      crate.addCollider({ shape: 'mesh', mesh: crateMesh });
    } else {
      crate.addCollider({ shape: 'box', size: [1, 1, 1] });
    }
  }

  const sphere = scene.createObject('Physics Sphere');
  sphere.setPosition(2.2, 2.4, 2);
  sphere.setScale(0.42, 0.42, 0.42);
  sphere.addModel({ mesh: engine.meshSphere(), color: [0.95, 0.48, 0.12, 1] });
  sphere.addRigidbody({ motion: 'dynamic', mass: 2, friction: 0.35, restitution: 0.35 });
  sphere.addCollider({ shape: 'sphere', radius: 0.5 });

  const sphere2d = scene.createObject('2D Physics Sphere');
  sphere2d.setPosition(-2.6, 2.8, -2.1);
  sphere2d.setScale(0.32, 0.32, 0.32);
  sphere2d.addModel({ mesh: engine.meshSphere(), color: [0.5, 0.92, 0.35, 1] });
  sphere2d.addRigidbody({ motion: 'dynamic', is2D: true, mass: 1 });
  sphere2d.addCollider({ shape: 'sphere', radius: 0.5 });

  const blueLight = scene.createObject('Blue Light');
  blueLight.setPosition(-2.2, 1.2, -1.2);
  blueLight.setScale(0.22, 0.22, 0.22);
  blueLight.addLight({ type: 'point', color: [0.2, 0.5, 1], intensity: 2.2, range: 8 });
  blueLight.addModel({ mesh: engine.meshSphere(), color: [0.15, 0.4, 1, 1] });

  const orangeLight = scene.createObject('Warm Light');
  orangeLight.setPosition(2.6, 1.0, -1.3);
  orangeLight.setScale(0.24, 0.24, 0.24);
  orangeLight.addLight({ type: 'point', color: [1, 0.42, 0.08], intensity: 2.4, range: 9 });
  orangeLight.addModel({ mesh: engine.meshSphere(), color: [1, 0.55, 0.1, 1] });

  // Static showcase prop using the multi-material OBJ (rendered with one
  // texture in the web demo).
  const showcase = scene.createObject('OBJ Showcase');
  showcase.setPosition(3.5, 0.35, -2.5);
  showcase.setEulerDeg(0, 25, 0);
  showcase.setScale(0.7, 0.7, 0.7);
  showcase.addModel({ mesh: crateMesh, texture: checkerTexture });

  let yaw = 0;
  let pitch = -0.08;
  let cameraPosition: [number, number, number] = [0, 1.7, 6.2];
  const sensitivity = 0.0022;
  let fpsTime = 0;
  let fpsFrames = 0;
  let fps = 0;

  const update = (dt: number) => {
    const input = engine.input;

    if (input.isMouseDown(0) && !input.pointerLocked) {
      input.requestPointerLock();
    }

    if (input.pointerLocked) {
      const [dx, dy] = input.mouseDelta();
      yaw -= dx * sensitivity;
      pitch = Math.min(1.45, Math.max(-1.45, pitch - dy * sensitivity));
    }

    const cosPitch = Math.cos(pitch);
    const forward: [number, number, number] = [
      -Math.sin(yaw) * cosPitch,
      Math.sin(pitch),
      -Math.cos(yaw) * cosPitch,
    ];
    const right: [number, number, number] = [Math.cos(yaw), 0, -Math.sin(yaw)];
    const move: [number, number, number] = [0, 0, 0];
    if (input.isDown('KeyW')) { move[0] += forward[0]; move[1] += forward[1]; move[2] += forward[2]; }
    if (input.isDown('KeyS')) { move[0] -= forward[0]; move[1] -= forward[1]; move[2] -= forward[2]; }
    if (input.isDown('KeyD')) { move[0] += right[0]; move[2] += right[2]; }
    if (input.isDown('KeyA')) { move[0] -= right[0]; move[2] -= right[2]; }
    if (input.isDown('Space')) move[1] += 1;
    if (input.isDown('ControlLeft')) move[1] -= 1;

    const length = Math.hypot(move[0], move[1], move[2]);
    if (length > 0.001) {
      const speed = input.isDown('ShiftLeft') ? 9 : 5;
      cameraPosition = [
        cameraPosition[0] + (move[0] / length) * speed * dt,
        cameraPosition[1] + (move[1] / length) * speed * dt,
        cameraPosition[2] + (move[2] / length) * speed * dt,
      ];
    }

    cameraObject.setPosition(...cameraPosition);
    cameraObject.setLookAngles(pitch, yaw, 0);

    fpsTime += dt;
    fpsFrames++;
    if (fpsTime >= 0.5) {
      fps = Math.round(fpsFrames / fpsTime);
      fpsTime = 0;
      fpsFrames = 0;
    }
  };

  status.textContent = '';
  status.classList.add('hidden');

  let hudTime = 0;
  const loop = (time: number) => {
    if (time - hudTime >= 250) {
      hudTime = time;
      const stats = engine.stats;
      hud.innerHTML = `
        <strong>EGN WebGPU</strong><br>
        WASM + WebGPU · ${stats.objects} objetos · ${stats.drawCalls} draws · ${stats.shadowCasters} sombras<br>
        ${fps} FPS · clique para travar o mouse · <kbd>WASD</kbd> mover · <kbd>Shift</kbd> correr · <kbd>Space/Ctrl</kbd> subir/descer
      `;
    }
    requestAnimationFrame(loop);
  };
  requestAnimationFrame(loop);

  canvas.addEventListener('click', () => {
    if (!engine.input.pointerLocked) engine.input.requestPointerLock();
  });

  engine.run(update);
  (window as unknown as { egn?: EGNT.Engine }).egn = engine;
}
