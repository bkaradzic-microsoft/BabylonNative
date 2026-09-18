const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");
const { test } = require("node:test");
const { runInNewContext } = require("node:vm");

const source = readFileSync(join(__dirname, "..", "Scripts", "validation_native.js"), "utf8");
// Exercise the real loader without starting the native host, font loader, or full suite.
const start = source.indexOf("    function loadPlayground(");
const end = source.indexOf("    function runTest(", start);
assert.ok(start >= 0 && end > start);
const loaderSource = source.slice(start, end);

function createHarness(code, { finishRendering = true, loadError = false, sceneFolder = false } = {}) {
    const observers = new Set();
    const timers = new Map();
    const results = [];
    const errors = [];
    let timerId = 0;
    let renderDone;
    let sceneLoaded;
    const scene = { disposed: 0, dispose() { this.disposed++; } };
    const engine = {
        onEffectErrorObservable: {
            add(observer) { observers.add(observer); return observer; },
            remove(observer) { observers.delete(observer); },
        },
        emitFailure({ terminal = true, ready = false, disposed = false } = {}) {
            for (const observer of [...observers]) {
                observer({
                    effect: { allFallbacksProcessed: () => terminal, isReady: () => ready, isDisposed: disposed },
                    errors: "NativeEngine does not support WGSL graphics shaders.",
                });
            }
        },
    };
    const context = {
        engine, scene,
        currentScene: null,
        config: { root: "app:///Scripts/" },
        console: { error: (error) => errors.push(String(error)) },
        setTimeout(callback, delay) {
            const id = ++timerId;
            timers.set(id, { callback, delay });
            return id;
        },
        clearTimeout(id) { timers.delete(id); },
        failTest(done) { done(false); },
        processCurrentScene(test, reference, done) {
            renderDone = done;
            if (finishRendering) { done(true); }
        },
        BABYLON: {
            Tools: {
                LoadFile(url, success, progress, database, binary, failure) {
                    if (loadError) { failure({ status: 404, statusText: "Not Found" }); }
                    else { success(JSON.stringify({ jsonPayload: JSON.stringify({ code }) })); }
                },
            },
            SceneLoader: {
                Load(root, file, targetEngine, success) { sceneLoaded = success; },
            },
        },
    };
    runInNewContext(loaderSource, context);
    context.loadPlayground(
        sceneFolder ? { title: "fixture", sceneFolder: "scenes/", sceneFilename: "test.babylon" } : { title: "fixture", playgroundId: "#test#1" },
        (status) => results.push(status),
        {},
        () => {}
    );
    return {
        engine, observers, timers, results, errors, context, scene,
        finishRender: () => renderDone(true),
        loadScene: () => sceneLoaded(scene),
        async flush() {
            for (let i = 0; i < 20; i++) {
                await new Promise(setImmediate);
                const due = [...timers].filter(([, timer]) => timer.delay === 0);
                if (!due.length) { return; }
                for (const [id, timer] of due) {
                    timers.delete(id);
                    timer.callback();
                }
            }
            assert.fail("Loader did not settle");
        },
    };
}

test("fails a pending scene promise on a terminal shader error and cancels its ten-minute timer", async () => {
    const h = createHarness("function createScene() { return new Promise(() => {}); }");
    await h.flush();
    assert.equal(h.timers.size, 1);
    h.engine.emitFailure();
    assert.deepEqual(h.results, [], "must not dispose inside the compiler notification");
    await h.flush();
    assert.deepEqual(h.results, [false]);
    assert.equal(h.observers.size, 0);
    assert.equal(h.timers.size, 0);
    assert.equal(h.context.currentScene, null, "a promise must not be treated as a disposable scene");
    assert.match(h.errors.join("\n"), /WGSL graphics shaders/);
});

test("handles shader failures emitted synchronously during scene construction", async () => {
    const h = createHarness("function createScene(engine) { engine.emitFailure(); return new Promise(() => {}); }");
    await h.flush();
    assert.deepEqual(h.results, [false]);
    assert.equal(h.timers.size, 0);
    assert.equal(h.observers.size, 0);
});

test("allows compilation fallbacks and a retained ready pipeline, ignoring disposed effects", async () => {
    const h = createHarness("function createScene() { return Promise.resolve(scene); }", { finishRendering: false });
    await h.flush();
    h.engine.emitFailure({ terminal: false });
    h.engine.emitFailure({ ready: true });
    h.engine.emitFailure({ disposed: true });
    await h.flush();
    assert.deepEqual(h.results, []);
    assert.deepEqual(h.errors, []);
    h.finishRender();
    assert.deepEqual(h.results, [true]);
    assert.equal(h.observers.size, 0);
    assert.equal(h.timers.size, 0);
});

test("fails post-creation shader errors only once and ignores later errors after cleanup", async () => {
    const h = createHarness("function createScene() { return scene; }", { finishRendering: false });
    await h.flush();
    h.engine.emitFailure();
    h.engine.emitFailure();
    await h.flush();
    h.engine.emitFailure();
    h.finishRender();
    assert.deepEqual(h.results, [false]);
    assert.equal(h.errors.length, 1);
    assert.equal(h.observers.size, 0);
    assert.equal(h.timers.size, 0);
});

test("does not pass if a terminal error arrives just before screenshot completion", async () => {
    const h = createHarness("function createScene() { return scene; }", { finishRendering: false });
    await h.flush();
    h.engine.emitFailure();
    h.finishRender();
    await h.flush();
    assert.deepEqual(h.results, [false]);
    assert.equal(h.timers.size, 0);
});

test("disposes a late-resolving scene without replacing the current scene after failure", async () => {
    const h = createHarness("function createScene() { return new Promise(resolve => { globalThis.resolveScene = resolve; }); }");
    await h.flush();
    h.engine.emitFailure();
    await h.flush();
    h.context.resolveScene(h.scene);
    await h.flush();
    assert.equal(h.scene.disposed, 1);
    assert.equal(h.context.currentScene, null);
    assert.deepEqual(h.results, [false]);
    assert.equal(h.timers.size, 0);
});

test("cleans up the shader observer after a snippet load failure", async () => {
    const h = createHarness("", { loadError: true });
    await h.flush();
    assert.deepEqual(h.results, [false]);
    assert.equal(h.observers.size, 0);
    assert.equal(h.timers.size, 0);
});

test("also fails scene-file loading and disposes a scene delivered after the failure", async () => {
    const h = createHarness("", { sceneFolder: true });
    h.engine.emitFailure();
    await h.flush();
    h.loadScene();
    assert.deepEqual(h.results, [false]);
    assert.equal(h.scene.disposed, 1);
    assert.equal(h.observers.size, 0);
});
