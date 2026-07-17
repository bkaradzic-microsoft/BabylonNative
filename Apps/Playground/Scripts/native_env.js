// Environment shims for the Native V8 harness.
//
// Browser playgrounds run in a page where a handful of bare globals always
// exist. The embedded V8 runtime has none of them, so a few otherwise-portable
// snippets and UMD side-bundles fail at load time. Define the minimum set here,
// loaded before any Babylon bundle.

// Some Babylon UMD side-bundles (e.g. the procedural-textures and earcut
// libraries) merge their classes into `BABYLON` via `global`/`window`. Point
// `global` at the runtime's global object so that merge runs. `window` is left
// undefined on purpose to avoid activating browser/DOM code paths.
if (typeof globalThis.global === "undefined") {
    globalThis.global = globalThis;
}

// Playground snippets frequently reference the bare `name` global (the browser's
// `window.name`, an empty string by default). Provide it so those snippets stop
// throwing ReferenceError.
if (typeof globalThis.name === "undefined") {
    globalThis.name = "";
}
