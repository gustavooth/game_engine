import createModule from "../../../engine.js";
import { WebGPURenderer } from "../../renderer/webgpu/renderer.js";

// Browser-specific engine backend. Game setup and frame logic live in V/WASM.
export async function start(canvas) {
  if (!navigator.gpu) throw new Error("WebGPU is unavailable in this browser");
  const module = await createModule({ noInitialRun: true });
  if (!module._egn_web_init()) throw new Error("failed to initialize engine");
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
  if (!adapter) throw new Error("no WebGPU adapter available");
  const device = await adapter.requestDevice();
  const context = canvas.getContext("webgpu");
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: "opaque" });
  const base = new URL("../../renderer/webgpu/shaders/", import.meta.url);
  const shaderNames = ["scene", "shadow", "particle", "sprite", "post"];
  const shaders = await Promise.all(shaderNames.map(async (name) => {
    const response = await fetch(new URL(`${name}.wgsl`, base));
    if (!response.ok) throw new Error(`failed to load shader: ${name}`);
    return response.text();
  }));
  const renderer = new WebGPURenderer(canvas, device, context, format, module, {
    scene: shaders[0], shadow: shaders[1], particle: shaders[2], sprite: shaders[3],
    post: shaders[4]
  });
  globalThis.egnWebRenderer = renderer;
  const controller = new AbortController();
  const listenerOptions = { signal: controller.signal };
  const keysDown = new Set();
  const buttonsDown = new Set();
  globalThis.egnWebDispose = () => {
    controller.abort();
    if (document.pointerLockElement === canvas) document.exitPointerLock();
    renderer.destroy();
    delete globalThis.egnWebRenderer;
    delete globalThis.egnWebDispose;
    delete globalThis.egnWantPointerLock;
  };
  canvas.addEventListener("contextmenu", (event) => event.preventDefault(), listenerOptions);
  window.addEventListener("keydown", (event) => {
    keysDown.add(event.keyCode);
    module._egn_web_input_key(event.keyCode, 1);
    if (["Space", "F1", "F2", "F12"].includes(event.code)) event.preventDefault();
  }, listenerOptions);
  window.addEventListener("keyup", (event) => { keysDown.delete(event.keyCode); module._egn_web_input_key(event.keyCode, 0); }, listenerOptions);
  const button = (value) => value === 2 ? 1 : value === 1 ? 2 : value;
  canvas.addEventListener("pointerdown", (event) => {
    buttonsDown.add(button(event.button));
    module._egn_web_input_button(button(event.button), 1);
    if (globalThis.egnWantPointerLock && document.pointerLockElement !== canvas)
      Promise.resolve(canvas.requestPointerLock()).catch(() => {});
  }, listenerOptions);
  window.addEventListener("pointerup", (event) => { buttonsDown.delete(button(event.button)); module._egn_web_input_button(button(event.button), 0); }, listenerOptions);
  window.addEventListener("pointermove", (event) => {
    const rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return;
    const x = (event.clientX - rect.left) * canvas.width / rect.width;
    const y = (event.clientY - rect.top) * canvas.height / rect.height;
    module._egn_web_input_move(x, y, document.pointerLockElement === canvas ? event.movementX : 0,
      document.pointerLockElement === canvas ? event.movementY : 0);
  }, listenerOptions);
  window.addEventListener("blur", () => {
    for (const key of keysDown) module._egn_web_input_key(key, 0);
    for (const key of buttonsDown) module._egn_web_input_button(key, 0);
    keysDown.clear(); buttonsDown.clear();
  }, listenerOptions);
  module.callMain([]);
  return { module, renderer };
}
