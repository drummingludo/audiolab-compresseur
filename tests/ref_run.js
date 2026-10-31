// Fait tourner le VRAI code al-comp d'Audio Lab (copie vérifiée par empreinte SHA-256)
// hors navigateur : simule AudioWorkletProcessor, traite par blocs de 128 échantillons.
// Usage : node ref_run.js <fs> <params.json> <in.f32> <out.f32>
const fs_ = require('fs'), path = require('path'), vm = require('vm');
const [fsRate, paramsFile, inFile, outFile] = process.argv.slice(2);
const src = fs_.readFileSync(path.join(__dirname, 'alcomp_ref.js'), 'utf8');
let Proc = null;
const ctx = {
  sampleRate: +fsRate, Math, Float32Array,
  AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
  registerProcessor: (name, cls) => { if (name === 'al-comp') Proc = cls; }
};
vm.createContext(ctx); vm.runInContext(src, ctx);
const params = JSON.parse(fs_.readFileSync(paramsFile, 'utf8'));
const node = new Proc();
node.port.onmessage({ data: params });
const raw = fs_.readFileSync(inFile);
const inter = new Float32Array(raw.buffer, raw.byteOffset, raw.length / 4);
const N = inter.length / 2, out = new Float32Array(N * 2);
const B = 128;
for (let s = 0; s < N; s += B) {
  const n = Math.min(B, N - s);
  const L = new Float32Array(n), R = new Float32Array(n), oL = new Float32Array(n), oR = new Float32Array(n);
  for (let i = 0; i < n; i++) { L[i] = inter[2 * (s + i)]; R[i] = inter[2 * (s + i) + 1]; }
  node.process([[L, R]], [[oL, oR]]);
  for (let i = 0; i < n; i++) { out[2 * (s + i)] = oL[i]; out[2 * (s + i) + 1] = oR[i]; }
}
fs_.writeFileSync(outFile, Buffer.from(out.buffer));
