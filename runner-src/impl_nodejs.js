"use strict";
// @ts-check
const fs = require('fs');
const WASI = require('wasi').WASI;

const argv = process.argv.slice(1, undefined);

const wasiOpts = {
    args: argv,
    env: process.env,
    preopens: {
        '/': '.'
    },
    returnOnExit: true,
    stdin: 0,
    stdout: 2,
    stderr: 2
};
let wasi = new WASI(wasiOpts);

const wasmContent = fs.readFileSync(argv[0]);
let wasmModule = new WebAssembly.Module(wasmContent);
let wasmInstance = new WebAssembly.Instance(wasmModule, { wasi_snapshot_preview1: wasi.wasiImport });

const preStep = () => {
    wasi = null;
    wasmInstance = null;
    global.gc();
    wasi = new WASI(wasiOpts);
    wasmInstance = new WebAssembly.Instance(wasmModule, { wasi_snapshot_preview1: wasi.wasiImport });
};
const step = () => {
    wasi.start(wasmInstance);
};

return [preStep, step];