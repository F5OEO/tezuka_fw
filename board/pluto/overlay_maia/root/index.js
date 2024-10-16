import init, { maia_wasm_start } from "./pkg/maia_wasm.js";

async function run() {
    await init();
    maia_wasm_start();
};

run();
