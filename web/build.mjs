import { build } from 'esbuild';
import { cp, copyFile, mkdir, readFile, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(root, '..');
const dist = path.join(root, 'dist');

await mkdir(dist, { recursive: true });

// 1. JavaScript SDK (bundled ESM) + TypeScript declarations.
await build({
  entryPoints: [path.join(root, 'sdk/index.ts')],
  outfile: path.join(root, 'sdk/egn.js'),
  bundle: true,
  format: 'esm',
  platform: 'browser',
  target: 'es2022',
  sourcemap: false,
  minify: false,
  legalComments: 'none',
  loader: { '.wgsl': 'text' },
  nodePaths: [path.join(root, 'node_modules')],
  banner: {
    js: '/* EGN Web SDK - generated from web/sdk/egn.ts. Do not edit by hand. */',
  },
});

execFileSync('npx', [
  'dts-bundle-generator',
  '--project', path.join(root, 'tsconfig.json'),
  '--no-check',
  '--no-banner',
  '--external-imports', 'wgpu-matrix',
  '-o', path.join(root, 'sdk/egn.d.ts'),
  path.join(root, 'sdk/index.ts'),
], { cwd: root, stdio: 'inherit' });

const declarationPath = path.join(root, 'sdk/egn.d.ts');
const declaration = await readFile(declarationPath, 'utf8');
if (!declaration.startsWith('/// <reference types="@webgpu/types" />')) {
  await writeFile(declarationPath, `/// <reference types="@webgpu/types" />\n${declaration}`);
}

// 2. Demo bundle. The SDK is supplied at runtime by index.html so it is not
// duplicated inside the demo bundle.
await build({
  entryPoints: [path.join(root, 'demo/main.ts')],
  outfile: path.join(dist, 'demo.js'),
  bundle: true,
  format: 'esm',
  platform: 'browser',
  target: 'es2022',
  sourcemap: true,
  legalComments: 'none',
  nodePaths: [path.join(root, 'node_modules')],
});

// 3. Public SDK and page artifacts.
await copyFile(path.join(root, 'sdk/egn.js'), path.join(dist, 'egn.js'));
await copyFile(path.join(root, 'sdk/egn.d.ts'), path.join(dist, 'egn.d.ts'));
await copyFile(path.join(root, 'demo/index.html'), path.join(dist, 'index.html'));

// 4. Runtime assets.
await cp(path.join(repo, 'game/assets'), path.join(dist, 'assets'), { recursive: true, force: true });

console.log('  WEB  web/dist ready');
