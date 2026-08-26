// ES2019 language polyfills for the (older Chakra) JS engine hosted by Babylon
// Native. These built-ins are absent at runtime, so playground snippets and
// Babylon core that use them throw "Object doesn't support property or method".
// Defined non-enumerable to avoid leaking into for-in enumeration.
(function () {
    function define(proto, name, fn) {
        if (!proto[name]) {
            Object.defineProperty(proto, name, { value: fn, writable: true, configurable: true, enumerable: false });
        }
    }
    define(String.prototype, "trimStart", function () { return this.replace(/^[\s\uFEFF\xA0]+/, ""); });
    define(String.prototype, "trimEnd", function () { return this.replace(/[\s\uFEFF\xA0]+$/, ""); });
    define(Array.prototype, "flat", function (depth) {
        var d = depth === undefined ? 1 : Math.floor(depth);
        if (isNaN(d) || d < 1) { return Array.prototype.slice.call(this); }
        return Array.prototype.reduce.call(this, function (acc, cur) {
            if (Array.isArray(cur)) { acc.push.apply(acc, cur.flat(d - 1)); } else { acc.push(cur); }
            return acc;
        }, []);
    });
    define(Array.prototype, "flatMap", function (cb, thisArg) {
        return Array.prototype.map.call(this, cb, thisArg).flat();
    });
})();

(function () {
    let currentScene;
    let config;
    const opts = (typeof _playgroundOptions === "object" && _playgroundOptions) ? _playgroundOptions : {};
    const justOnce = !!opts.runOnce;
    const saveResult = (typeof opts.saveResults === "boolean") ? opts.saveResults : true;
    const testWidth = 600;
    const testHeight = 400;
    const generateReferences = !!opts.generateReferences;
    const breakOnFail = !!opts.breakOnFail;
    const stopOnFirstFailure = !!opts.stopOnFirstFailure;
    const listTests = !!opts.listTests;
    const includeExcluded = !!opts.includeExcluded;
    const testFilters = Array.isArray(opts.testFilters) ? opts.testFilters.map(s => String(s).toLowerCase()) : [];
    const testIndices = Array.isArray(opts.testIndices) ? opts.testIndices.map(n => +n) : [];
    // CLI --capture=N: 1-based frame index at which to call
    // TestUtils.captureNextFrame() for every executed test. The runner
    // extends each test's render budget so the .rdc finalizes.
    const cliCaptureFrame = (typeof opts.captureFrame === "number" && opts.captureFrame > 0) ? (opts.captureFrame | 0) : 0;
    // Frames after the trigger to let RenderDoc finalize the .rdc.
    const POST_CAPTURE_FRAMES = 5;
    // Upper bound on ticks spent waiting for a scene to converge after
    // executeWhenReady fired, so a scene that never settles fails on a stale
    // frame rather than hanging. See isSceneConverged.
    const MAX_WARMUP_FRAMES = 240;

    function shouldRunTest(test, index) {
        if (testIndices.length > 0 && testIndices.indexOf(index) === -1) {
            return false;
        }
        if (testFilters.length > 0) {
            const title = (test.title || "").toLowerCase();
            for (let i = 0; i < testFilters.length; ++i) {
                if (title.indexOf(testFilters[i]) !== -1) {
                    return true;
                }
            }
            return false;
        }
        return true;
    }

    function failTest(done) {
        if (breakOnFail) {
            // Trigger the JS debugger if attached; on no-debugger runs the
            // host's bx exception filter prints a callstack on the next throw.
            // eslint-disable-next-line no-debugger
            debugger;
        }
        done(false);
    }

    // Emitted after a pixel-comparison failure to make triage faster. Prints the
    // rendered/diff PNG paths plus a re-run command. For scenes fetched from the
    // snippet server it also notes that assets/fonts arrive over the network, so
    // async load timing is one possible cause -- but it is only one of several,
    // and it is cheap to rule out: a timing flake varies run to run, while a real
    // regression reproduces with an identical pixel-difference count. Say that
    // explicitly. The previous wording called such diffs "often a transient flake",
    // which led to a genuine, perfectly reproducible motion-blur regression being
    // waved off for two weeks of nightlies.
    function logFailureDiagnostics(test) {
        const outDir = TestUtils.getOutputDirectory();
        if (test.referenceImage) {
            console.log(`  Rendered result: ${outDir}/Results/${test.referenceImage}`);
            console.log(`  Diff overlay:    ${outDir}/Errors/${test.referenceImage}`);
        }
        if (test.playgroundId) {
            console.log(`  Note: this test loads playgroundId ${test.playgroundId} from the snippet server and pulls GUI/assets/fonts over the network, so async asset/font-load timing is one possible cause of a pixel diff.`);
        }
        console.log("  Re-run in isolation; an identical pixel count on repeat runs means a real regression, not a timing flake:");
        console.log(`    Playground --headless --once --test "${test.title || ""}" app:///Scripts/validation_native.js`);
    }

    // Per-run counters surfaced as a final summary line on exit.
    let ranCount = 0;
    let passedCount = 0;
    let failedCount = 0;
    let skippedCount = 0;
    let missingRefCount = 0;
    const failedTitles = [];

    function getExclusionReason(t) {
        if (t.onlyVisual) {
            return "onlyVisual";
        }
        if (t.excludeFromAutomaticTesting) {
            return "excludeFromAutomaticTesting" + (t.reason ? ": " + t.reason : "");
        }
        if (t.excludedGraphicsApis && t.excludedGraphicsApis.includes(TestUtils.getGraphicsApiName())) {
            return "excludedGraphicsApis: " + TestUtils.getGraphicsApiName();
        }
        return null;
    }

    function getSkipReason(t) {
        if (includeExcluded) {
            return null;
        }
        return getExclusionReason(t);
    }

    function logRunSummary() {
        console.log("Run complete. ran=" + ranCount +
                    " passed=" + passedCount +
                    " failed=" + failedCount +
                    " missingRef=" + missingRefCount +
                    " skipped=" + skippedCount);
        if (failedTitles.length > 0) {
            console.log("Failed tests (" + failedTitles.length + "):");
            for (let n = 0; n < failedTitles.length; n++) {
                console.log("  - " + failedTitles[n]);
            }
        }
    }

    const engine = new BABYLON.NativeEngine();
    globalThis.engine = engine;
    engine.getCaps().parallelShaderCompile = undefined;

    // Broaden Babylon's default retry strategy for the test framework: in addition to
    // network drops (status 0, the default trigger), also retry transient HTTP errors
    // (5xx) and rate limits (429). Applies to every BABYLON.Tools.LoadFile request
    // including the snippet fetches in loadPG below and the texture/asset loads
    // initiated from inside each playground's createScene().
    BABYLON.Tools.DefaultRetryStrategy = function (url, request, retryIndex) {
        const maxRetries = 5;
        if (retryIndex >= maxRetries) {
            return -1;
        }
        if (url.indexOf("file:") !== -1) {
            return -1;
        }
        if (request.status === 0 ||
            request.status === 429 ||
            (request.status >= 500 && request.status < 600)) {
            return Math.pow(2, retryIndex) * 500;
        }
        return -1;
    };

    engine.getRenderingCanvas = function () {
        return window;
    }

    // getRenderingCanvas() above hands out the window object as the "canvas", so playgrounds that
    // reach for HTMLCanvasElement members find them missing and throw. Add the element-ish surface
    // they actually use (style for CSS tweaks, focus/blur for input tests) on window itself rather
    // than returning a wrapper, since input handling elsewhere compares against window by identity.
    if (!window.style) {
        window.style = {};
    }
    if (typeof window.focus !== "function") {
        window.focus = function () { };
    }
    if (typeof window.blur !== "function") {
        window.blur = function () { };
    }
    if (!window.screen) {
        // Desktop has no device-orientation sensor, so a fixed landscape screen at
        // angle 0 is what freeCameraDeviceOrientationInput would compute anyway.
        window.screen = {
            width: engine.getRenderWidth(),
            height: engine.getRenderHeight(),
            availWidth: engine.getRenderWidth(),
            availHeight: engine.getRenderHeight(),
            colorDepth: 24,
            pixelDepth: 24,
            orientation: { angle: 0, type: "landscape-primary" }
        };
    }

    engine.getInputElement = function () {
        return 0;
    }

    // Native drives input through NativeDeviceInputSystem, which polls _native rather than
    // subscribing to DOM events, so a dispatched pointer event would reach nothing. Playgrounds
    // that drive picking synthetically (canvas.dispatchEvent(new PointerEvent(...))) therefore
    // need the harness to hand the event to the same InputManager entry points the
    // WebDeviceInputSystem would drive. Going through _onPointerDown/Move/Up rather than the
    // public simulatePointer* matters: those take a caller-supplied PickingInfo and so bypass
    // scene.skipPointerDownPicking / skipPointerUpPicking / skipPointerMovePicking, which is
    // exactly what several of these tests exist to exercise.
    const POINTER_INPUT_MOVE = 12;
    const domListeners = new Map();
    window.addEventListener = function (type, listener) {
        if (typeof listener !== "function") {
            return;
        }
        if (!domListeners.has(type)) {
            domListeners.set(type, []);
        }
        domListeners.get(type).push(listener);
    };
    window.removeEventListener = function (type, listener) {
        const list = domListeners.get(type);
        if (list) {
            const at = list.indexOf(listener);
            if (at !== -1) {
                list.splice(at, 1);
            }
        }
    };
    window.dispatchEvent = function (evt) {
        if (!evt) {
            return true;
        }

        if (evt.target === null || evt.target === undefined) {
            evt.target = window;
        }

        const list = domListeners.get(evt.type);
        if (list) {
            for (const listener of list.slice()) {
                listener.call(window, evt);
            }
        }

        const inputManager = currentScene && currentScene._inputManager;
        if (inputManager) {
            if (evt.button === undefined) {
                evt.button = 0;
            }
            switch (evt.type) {
                case "pointermove":
                    evt.inputIndex = POINTER_INPUT_MOVE;
                    inputManager._onPointerMove(evt);
                    break;
                case "pointerdown":
                    evt.inputIndex = evt.button + 2;
                    inputManager._onPointerDown(evt);
                    break;
                case "pointerup":
                    evt.inputIndex = evt.button + 2;
                    inputManager._onPointerUp(evt);
                    break;
                case "keydown":
                    inputManager._onKeyDown(evt);
                    break;
                case "keyup":
                    inputManager._onKeyUp(evt);
                    break;
            }
        }

        return !evt.defaultPrevented;
    };

    const canvas = window;
    globalThis.canvas = canvas;

    // Random replacement
    let seed = 1;
    Math.random = function () {
        const x = Math.sin(seed++) * 10000;
        return x - Math.floor(x);
    }

    function compare(test, renderData, referenceImage, threshold, errorRatio) {
        const referenceData = TestUtils.getImageData(referenceImage);
        if (referenceData.length != renderData.length) {
            throw new Error(`Reference data length (${referenceData.length}) must match render data length (${renderData.length})`);
        }

        const size = renderData.length;
        let differencesCount = 0;

        for (let index = 0; index < size; index += 4) {
            if (Math.abs(renderData[index] - referenceData[index]) < threshold &&
                Math.abs(renderData[index + 1] - referenceData[index + 1]) < threshold &&
                Math.abs(renderData[index + 2] - referenceData[index + 2]) < threshold) {
                continue;
            }

            if (differencesCount === 0) {
                const pixel = index / 4;
                const width = Math.round(testWidth / engine.getHardwareScalingLevel());
                console.log(`First pixel off at ${index} (pixel ${pixel} @ x=${pixel % width}, y=${Math.floor(pixel / width)}): Value: (${renderData[index]}, ${renderData[index + 1]}, ${renderData[index + 2]}) - Expected: (${referenceData[index]}, ${referenceData[index + 1]}, ${referenceData[index + 2]}) `);
            }

            referenceData[index] = 255;
            referenceData[index + 1] *= 0.5;
            referenceData[index + 2] *= 0.5;
            differencesCount++;
        }

        if (differencesCount) {
            const pixelCount = size / 4;
            const diffRatio = (differencesCount * 100) / pixelCount;
            console.log(`Pixel difference: ${differencesCount} / ${pixelCount} pixels (${diffRatio.toFixed(3)}%, per-channel threshold ${threshold}); allowed errorRatio ${errorRatio}%.`);
        } else {
            console.log("No pixel difference!");
        }

        const error = (differencesCount * 100) / (size / 4) > errorRatio;

        const width = testWidth / engine.getHardwareScalingLevel();
        const height = testHeight / engine.getHardwareScalingLevel();

        if (error) {
            TestUtils.writePNG(referenceData, width, height, TestUtils.getOutputDirectory() + "/Errors/" + test.referenceImage);
        }
        if (saveResult || error) {
            TestUtils.writePNG(renderData, width, height, TestUtils.getOutputDirectory() + "/Results/" + test.referenceImage);
        }
        return error;
    }

    function saveRenderedResult(test, renderData) {
        const width = testWidth / engine.getHardwareScalingLevel();
        const height = testHeight / engine.getHardwareScalingLevel();
        TestUtils.writePNG(renderData, width, height, TestUtils.getOutputDirectory() + "/Results/" + test.referenceImage);
        return false; // no error
    }

    // The reference images are captured in a browser where the canvas is styled
    // `background: greenyellow !important` (packages/tools/babylonServer/public/empty.html), so a
    // scene that clears to a translucent color is composited over greenyellow by the page before
    // the screenshot is taken. Native reads the framebuffer back directly and never sees that
    // backdrop, so every such test differed by its whole background. Replicate the browser's
    // compositing here: straight (non-premultiplied) source-over against greenyellow.
    //
    // This is deliberately gated on the scene's clearColor alpha rather than applied to every
    // frame. Several tests (additive/multiply particles, area lights, some prepass post-processes)
    // leave alpha < 1 in the framebuffer even though they cleared opaque; the browser canvas is
    // opaque in those cases so the backdrop is never visible, and compositing them unconditionally
    // tinted ~19 passing tests green. Native writing an unexpected alpha there is a separate bug.
    const CANVAS_BACKGROUND = [173, 255, 47];

    function compositeOverCanvasBackground(data) {
        for (let index = 0; index < data.length; index += 4) {
            const alpha = data[index + 3];
            if (alpha === 255) {
                continue;
            }
            const src = alpha / 255;
            const dst = 1 - src;
            data[index] = Math.round(data[index] * src + CANVAS_BACKGROUND[0] * dst);
            data[index + 1] = Math.round(data[index + 1] * src + CANVAS_BACKGROUND[1] * dst);
            data[index + 2] = Math.round(data[index + 2] * src + CANVAS_BACKGROUND[2] * dst);
            data[index + 3] = 255;
        }
        return data;
    }

    function evaluateScreenshot(test, screenshot, referenceImage, done, compareFunction) {
        let testRes = true;

        if (currentScene && currentScene.clearColor && currentScene.clearColor.a < 1) {
            compositeOverCanvasBackground(screenshot);
        }

        if (!test.onlyVisual) {

            const defaultErrorRatio = 2.5;

            if (compareFunction(test, screenshot, referenceImage, test.threshold || 25, test.errorRatio || defaultErrorRatio)) {
                testRes = false;
                console.log("Test '" + (test.title || "(unnamed)") + "' failed (pixel comparison)");
                logFailureDiagnostics(test);
            } else {
                testRes = true;
                console.log("Test '" + (test.title || "(unnamed)") + "' validated");
            }
        }

        currentScene.dispose();
        currentScene = null;

        // A test can leave extra scenes behind (an async load that created its own scene, a scene
        // whose creation promise resolved after validation, ...). They stay registered on the
        // reused engine and keep their resources alive, so dispose them here.
        const strayScenes = engine.scenes.slice();
        for (let i = 0; i < strayScenes.length; ++i) {
            strayScenes[i].dispose();
        }

        engine.setHardwareScalingLevel(1);

        // Reset render state that persists on the reused engine so each test starts fresh.
        // A test that leaves the stencil test enabled or a scissor rect set would otherwise
        // corrupt later tests (e.g. the glow-layer test).
        engine.setStencilBuffer(false);
        engine.disableScissor();

        // This is necessary because of https://github.com/BabylonJS/Babylon.js/pull/15217 so that each test starts fresh.
        engine.releaseEffects();

        // Textures are cached on the engine by URL (BaseTexture._getFromCache), and the cache key
        // covers only url/noMipmap/isCube -- not the load-time options. A test that leaves a
        // reference behind (e.g. assigning one texture to both scene.environmentTexture and a
        // material's reflectionTexture) keeps its internal texture in that cache across
        // scene.dispose(), so a later test loading the same URL silently reuses the *previous*
        // test's texture along with its prefiltering/irradiance settings. Release whatever is
        // left so every test loads its own textures and results do not depend on run order.
        const leakedTextures = engine.getLoadedTexturesCache();
        for (let i = leakedTextures.length - 1; i >= 0; --i) {
            engine._releaseTexture(leakedTextures[i]);
        }
        engine.clearInternalTexturesCache();

        // SceneLoader.OnPluginActivatedObservable is global and outlives the scene. Snippets use it
        // to configure the glTF loader (animationStartMode, compileMaterials, ...) and never
        // unregister, so without this every later glTF test would inherit those settings. The
        // browser harness reloads the page per test and never sees this; here the engine is reused.
        BABYLON.SceneLoader.OnPluginActivatedObservable.clear();

        done(testRes);
    }

    function evaluate(test, referenceImage, done, compareFunction) {
        TestUtils.getFrameBufferData(function (screenshot) {
            evaluateScreenshot(test, screenshot, referenceImage, done, compareFunction);
        });
    }

    // Scene.isReady() is not a reliable "the next frame will look right" signal.
    // Materials support shader hot-swapping: when their defines change, PBR keeps
    // the previous (ready) effect, calls defines.markAsUnprocessed() and still
    // reports the submesh as ready, so Scene.isReady() returns true while a
    // recompile is outstanding. Material._isReadyForSubMesh then memoizes that
    // answer via defines._renderId === scene.getRenderId(), and scene.render()
    // bumps the render id - so the very next draw re-evaluates, finds the new
    // effect still compiling, and Scene.render() silently *skips* the submesh.
    // The result is a frame with meshes missing.
    //
    // Return true only when every submesh has settled: no dirty defines and a
    // ready effect.
    function isSceneConverged(scene) {
        if (!scene.isReady()) {
            return false;
        }
        for (let i = 0; i < scene.meshes.length; i++) {
            const mesh = scene.meshes[i];
            if (!mesh.isEnabled() || !mesh.subMeshes || mesh.subMeshes.length === 0) {
                continue;
            }
            for (let j = 0; j < mesh.subMeshes.length; j++) {
                const subMesh = mesh.subMeshes[j];
                const defines = subMesh.materialDefines;
                if (defines && defines.isDirty) {
                    return false;
                }
                const effect = subMesh.effect;
                if (effect && !effect.isReady()) {
                    return false;
                }
            }
        }
        return true;
    }

    function processCurrentScene(test, renderImage, done, compareFunction) {
        currentScene.useConstantAnimationDeltaTime = true;
        // Frame at which to read back the framebuffer & validate. This is the
        // test's renderCount (default 1) and determines pass/fail. NOT shifted
        // by --capture.
        const compareFrame = test.renderCount || 1;
        // Frame at which to call TestUtils.captureNextFrame(), or 0 if no
        // capture is requested. CLI --capture=N takes precedence over the
        // per-test "capture" config flag; the legacy per-test flag triggers
        // at compareFrame.
        const captureFrame = cliCaptureFrame > 0
            ? cliCaptureFrame
            : (test.capture ? compareFrame : 0);
        // Stop after this many frames. With --capture we keep rendering past
        // compareFrame so RenderDoc can finalize the .rdc.
        const stopFrame = captureFrame > 0
            ? Math.max(compareFrame, captureFrame + POST_CAPTURE_FRAMES)
            : compareFrame;

        let frameIndex = 0;
        let stopped = false;
        let pendingScreenshot = null;
        let evaluated = false;
        let warmupFrames = 0;

        const runEvaluation = function (screenshot) {
            if (evaluated) {
                return;
            }
            evaluated = true;
            evaluateScreenshot(test, screenshot, renderImage, done, compareFunction);
        };

        // Babylon's Scene.executeWhenReady gives up after Scene.onReadyTimeoutDuration
        // (default 120s): once that elapses it fires onReadyTimeoutObservable and
        // silently drops the executeWhenReady callback. Some validation scenes load
        // very large assets (e.g. the EXR Loader's 3240x4800 RGBA32F image, whose
        // gamma-correct CPU mip generation takes ~3 min under ASAN on the 2-core CI
        // runner), which legitimately exceeds 120s. Without this the callback is
        // dropped, the render loop never starts, and the test hangs until the CI
        // job times out. Extend the budget generously and convert a genuine
        // never-ready scene into a fast test failure instead of a silent hang.
        currentScene.onReadyTimeoutDuration = 10 * 60 * 1000;
        currentScene.onReadyTimeoutObservable.addOnce(function () {
            console.error("Scene '" + (test.title || "?") + "' did not become ready within " +
                (currentScene.onReadyTimeoutDuration / 1000) + "s.");
            failTest(done);
        });

        currentScene.executeWhenReady(function () {
            if (currentScene.activeCamera && currentScene.activeCamera.useAutoRotationBehavior) {
                currentScene.activeCamera.useAutoRotationBehavior = false;
            }
            engine.runRenderLoop(function () {
                try {
                    // Wait for the scene to actually converge before starting the
                    // frame count (see isSceneConverged).
                    //
                    // Deliberately do NOT call scene.render() here: several tests
                    // set renderCount > 1 and compare an animated frame (particles,
                    // motion blur), so an extra rendered frame would shift the
                    // animation phase and change the captured image. Effect
                    // recompilation is driven by isReadyForSubMesh - which
                    // isSceneConverged already calls via scene.isReady() - and not
                    // by rendering. Material._isReadyForSubMesh memoizes its answer
                    // per render id, so bump the render id to force a fresh
                    // evaluation on the next tick, exactly as Scene._checkIsReady
                    // does while polling.
                    if (!stopped && warmupFrames < MAX_WARMUP_FRAMES && !isSceneConverged(currentScene)) {
                        warmupFrames++;
                        currentScene.incrementRenderId();
                        return;
                    }

                    frameIndex++;

                    if (captureFrame > 0 && frameIndex === captureFrame && TestUtils.captureNextFrame) {
                        TestUtils.captureNextFrame();
                    }

                    currentScene.render();

                    if (frameIndex === compareFrame) {
                        // Queue the framebuffer readback. The callback runs
                        // asynchronously; safe to dispose the scene from it
                        // but only after stopRenderLoop() has been called.
                        TestUtils.getFrameBufferData(function (data) {
                            if (stopped) {
                                runEvaluation(data);
                            } else {
                                pendingScreenshot = data;
                            }
                        });
                    }

                    if (frameIndex >= stopFrame && !stopped) {
                        stopped = true;
                        engine.stopRenderLoop();
                        if (pendingScreenshot !== null) {
                            // Defer dispose to next tick so it runs outside
                            // this runRenderLoop iteration.
                            const data = pendingScreenshot;
                            pendingScreenshot = null;
                            setTimeout(function () { runEvaluation(data); }, 0);
                        }
                    }
                }
                catch (e) {
                    console.error(e);
                    failTest(done);
                }
            });
        }, true);
    }

    function loadPlayground(test, done, referenceImage, compareFunction) {
        if (test.sceneFolder) {
            BABYLON.SceneLoader.Load(config.root + test.sceneFolder, test.sceneFilename, engine, function (newScene) {
                currentScene = newScene;
                processCurrentScene(test, referenceImage, done, compareFunction);
            },
                null,
                function (loadedScene, msg) {
                    console.error(msg);
                    failTest(done);
                });
        }
        else if (test.playgroundId) {
            if (test.playgroundId[0] !== "#" || test.playgroundId.indexOf("#", 1) === -1) {
                test.playgroundId += "#0";
            }

            const snippetUrl = "https://snippet.babylonjs.com";
            const pgRoot = "https://playground.babylonjs.com";

            const loadPG = function () {
                const url = snippetUrl + test.playgroundId.replace(/#/g, "/");
                BABYLON.Tools.LoadFile(
                    url,
                    function (responseText) {
                        try {
                            const snippet = JSON.parse(responseText);
                            let code = JSON.parse(snippet.jsonPayload).code.toString();

                            // Check if this is a v2 manifest and extract the entry file's code
                            // TODO: Handle multi-file playgrounds
                            try {
                                const manifestPayload = JSON.parse(code);
                                if (manifestPayload.v === 2) {
                                    code = manifestPayload.files[manifestPayload.entry]
                                        .replace(/export +default +/g, "")
                                        .replace(/export +/g, "");
                                }
                            } catch (e) {
                                // Not a manifest, proceed as usual
                            }

                            code = code
                                .replace(/"\/textures\//g, '"' + pgRoot + "/textures/")
                                .replace(/'\/textures\//g, "'" + pgRoot + "/textures/")
                                .replace(/"textures\//g, '"' + pgRoot + "/textures/")
                                .replace(/'textures\//g, "'" + pgRoot + "/textures/")
                                .replace(/\/scenes\//g, pgRoot + "/scenes/")
                                .replace(/"scenes\//g, '"' + pgRoot + "/scenes/")
                                .replace(/'scenes\//g, "'" + pgRoot + "/scenes/")
                                .replace(/"\.\.\/\.\.https/g, '"' + "https")
                                .replace("http://", "https://");

                            if (test.replace) {
                                const split = test.replace.split(",");
                                for (let i = 0; i < split.length; i += 2) {
                                    const source = split[i].trim();
                                    const destination = split[i + 1].trim();
                                    code = code.replace(source, destination);
                                }
                            }

                            const pgCode = code + "\r\ncreateScene(engine)";
                            // Defer scene construction to a fresh macrotask so
                            // eval()/createScene() run at a shallow native-stack
                            // depth instead of nested inside the native snippet
                            // load callback. Deep scenes otherwise pile onto the
                            // native XHR dispatch frames and can overflow engines
                            // with a small C stack (e.g. QuickJS).
                            setTimeout(async function () {
                                // eslint-disable-next-line no-unused-vars
                                var name = ""; // see the note on the scriptToRun eval below
                                try {
                                    // Runs before the first await, so the eval still happens at the
                                    // shallow stack depth this setTimeout exists to provide.
                                    currentScene = eval(pgCode);

                                    if (currentScene && currentScene.then) {
                                        // Handle if createScene returns a promise. Guard against a
                                        // snippet whose promise never resolves (e.g. a scene whose
                                        // utility-layer executeWhenReady never fires on Native): the
                                        // onReadyTimeout safety net lives inside processCurrentScene
                                        // and only applies AFTER the promise resolves, so without this
                                        // a pending createScene promise hangs the whole suite. Mirror
                                        // onReadyTimeoutDuration and convert it to a fast failure.
                                        // Note: this only fires if the JS event loop keeps running; a
                                        // snippet that blocks the JS thread natively (e.g. manual
                                        // setInterval frame-driving) is not rescued by this.
                                        const createSceneTimeoutMs = 10 * 60 * 1000;
                                        let createSceneTimeoutId;
                                        try {
                                            currentScene = await Promise.race([
                                                currentScene,
                                                new Promise(function (resolve, reject) {
                                                    createSceneTimeoutId = setTimeout(function () {
                                                        reject(new Error("createScene promise for " + test.playgroundId +
                                                            " did not resolve within " + (createSceneTimeoutMs / 1000) + "s."));
                                                    }, createSceneTimeoutMs);
                                                })
                                            ]);
                                        }
                                        finally {
                                            // Always clear it: a pending timer would otherwise keep the
                                            // event loop alive for the full timeout after a scene that
                                            // resolved normally.
                                            clearTimeout(createSceneTimeoutId);
                                        }
                                    }

                                    processCurrentScene(test, referenceImage, done, compareFunction);
                                }
                                catch (e) {
                                    console.error("Failed to evaluate playground snippet " + test.playgroundId + ": " + e);
                                    failTest(done);
                                }
                            }, 0);
                        }
                        catch (e) {
                            console.error("Failed to evaluate playground snippet " + test.playgroundId + ": " + e);
                            failTest(done);
                        }
                    },
                    undefined,  // onProgress
                    undefined,  // database
                    false,      // useArrayBuffer (snippet response is JSON text)
                    function (request, exception) {
                        const status = request ? (request.status + " " + request.statusText) : "no response";
                        console.error("Failed to load playground snippet " + test.playgroundId + " after retries: " + status);
                        if (exception) {
                            console.error(exception);
                        }
                        failTest(done);
                    }
                );
            }
            loadPG();
        } else {
            // Fix references
            if (test.specificRoot) {
                BABYLON.Tools.BaseUrl = config.root + test.specificRoot;
            }

            const request = new XMLHttpRequest();
            request.open('GET', config.root + test.scriptToRun, true);

            request.onreadystatechange = function () {
                if (request.readyState === 4) {
                    try {
                        request.onreadystatechange = null;

                        let scriptToRun = request.responseText.replace(/..\/..\/assets\//g, config.root + "/Assets/");
                        scriptToRun = scriptToRun.replace(/..\/..\/Assets\//g, config.root + "/Assets/");
                        scriptToRun = scriptToRun.replace(/\/assets\//g, config.root + "/Assets/");

                        if (test.replace) {
                            const split = test.replace.split(",");
                            for (let i = 0; i < split.length; i += 2) {
                                const source = split[i].trim();
                                const destination = split[i + 1].trim();
                                scriptToRun = scriptToRun.replace(source, destination);
                            }
                        }

                        if (test.replaceUrl) {
                            const split = test.replaceUrl.split(",");
                            for (let i = 0; i < split.length; i++) {
                                const source = split[i].trim();
                                const regex = new RegExp(source, "g");
                                scriptToRun = scriptToRun.replace(regex, config.root + test.rootPath + source);
                            }
                        }

                        const scriptCode = scriptToRun + test.functionToCall + "(engine)";
                        // Defer scene construction to a fresh macrotask so
                        // eval()/<functionToCall>() run at a shallow native-stack
                        // depth instead of nested inside the native XHR
                        // completion callback. Deep scenes otherwise pile onto
                        // the native XHR dispatch frames and can overflow engines
                        // with a small C stack (e.g. QuickJS).
                        setTimeout(function () {
                            // Browser scripts sometimes reference `name` without declaring it. In a
                            // page that silently resolves to window.name (""), so the mistake is
                            // invisible there but throws "ReferenceError: name is not defined"
                            // here. eval() below is a *direct* eval, so the evaluated script sees
                            // this function's scope and finds this binding -- same as it would on
                            // the web, without leaking an actual global. (A real global `name`
                            // is not an option: it breaks the Babylon UMD bundles at load time.)
                            // eslint-disable-next-line no-unused-vars
                            var name = "";
                            try {
                                currentScene = eval(scriptCode);
                                processCurrentScene(test, referenceImage, done, compareFunction);
                            }
                            catch (e) {
                                console.error(e);
                                failTest(done);
                            }
                        }, 0);
                    }
                    catch (e) {
                        console.error(e);
                        failTest(done);
                    }
                }
            };
            request.onerror = function () {
                console.error("Network error during test load.");
                failTest(done);
            }

            request.send(null);
        }
    }
    function runTest(index, done) {
        if (index >= config.tests.length) {
            done(false);
        }

        const test = config.tests[index];
        const testInfo = "Running " + test.title;
        console.log(testInfo);
        TestUtils.setTitle(testInfo);

        seed = 1;

        if (generateReferences) {
            loadPlayground(test, done, undefined, saveRenderedResult);
        } else {
            // Config validation: missing 'referenceImage' field is a permanent
            // catalog error (not a runtime asset-missing case), so short-circuit
            // before issuing the load. onlyVisual tests skip pixel comparison
            // so they don't need the reference image to exist.
            if (!test.onlyVisual && !test.referenceImage) {
                console.error("MISSING_REFERENCE_IMAGE: Test '" + (test.title || "(unnamed)") +
                              "' has no 'referenceImage' field in config.json - cannot run pixel comparison.");
                missingRefCount++;
                failTest(done);
                return;
            }

            // run test and image comparison
            const url = "app:///ReferenceImages/" + test.referenceImage;

            const onLoadFileError = function (request, exception) {
                // Reference-image load failures (missing file on disk, etc.)
                // arrive here via JsRuntimeHost's XHR error event +
                // BABYLON.Tools.LoadFile's onLoadFileError callback. Tag with
                // MISSING_REFERENCE_IMAGE: so CI greps still match.
                console.error("MISSING_REFERENCE_IMAGE: Test '" + (test.title || "(unnamed)") +
                              "' failed to load reference at " + url + ". " +
                              (exception ? exception : "(no exception details)"));
                missingRefCount++;
                failTest(done);
            };

            const onload = function (data, responseURL) {
                if (typeof (data) === "string") {
                    throw new Error("Decode Image from string data not yet implemented.");
                }

                const referenceImage = TestUtils.decodeImage(data);
                loadPlayground(test, done, referenceImage, compare);
            };

            BABYLON.Tools.LoadFile(url, onload, undefined, undefined, /*useArrayBuffer*/true, onLoadFileError);
        }
    }

    OffscreenCanvas = function (width, height) {
        return {
            width: width
            , height: height
            , getContext: function (type) {
                return {
                    fillRect: function (x, y, w, h) { }
                    , measureText: function (text) { return 8; }
                    , fillText: function (text, x, y) { }
                };
            }
        };
    }

    // The Canvas polyfill registers its Image implementation on `_native` rather than as a global,
    // but DOM code constructs it by its standard name - Particles/flowMap.ts FromUrlAsync does
    // `new Image()` - so expose it under that name too.
    if (typeof globalThis.Image === "undefined" && typeof _native !== "undefined" && _native.Image) {
        globalThis.Image = _native.Image;
    }

    if (typeof globalThis.KeyboardEvent === "undefined") {
        // Same rationale as PointerEvent below: the input manager only reads plain state
        // (key, code, keyCode, modifier flags) off keyboard events.
        globalThis.KeyboardEvent = function (type, init) {
            this.type = type;
            for (const key in (init || {})) {
                this[key] = init[key];
            }
            if (this.key === undefined) { this.key = ""; }
            if (this.code === undefined) { this.code = ""; }
            if (this.keyCode === undefined) { this.keyCode = 0; }
            if (this.ctrlKey === undefined) { this.ctrlKey = false; }
            if (this.altKey === undefined) { this.altKey = false; }
            if (this.shiftKey === undefined) { this.shiftKey = false; }
            if (this.metaKey === undefined) { this.metaKey = false; }
            if (this.repeat === undefined) { this.repeat = false; }
            this.target = null;
            this.defaultPrevented = false;
            this.preventDefault = function () { this.defaultPrevented = true; };
            this.stopPropagation = function () { };
            this.stopImmediatePropagation = function () { };
        };
    }

    // Babylon builds PointerEvents itself for Scene.simulatePointerDown/Move/Up. There is no DOM
    // here, and the input manager only ever reads plain state off the event (pointerId, button,
    // client coords, ...) and assigns inputIndex to it, so a data-holder is sufficient.
    if (typeof globalThis.PointerEvent === "undefined") {
        globalThis.PointerEvent = function (type, init) {
            this.type = type;
            for (const key in (init || {})) {
                this[key] = init[key];
            }
            if (this.pointerId === undefined) { this.pointerId = 1; }
            if (this.pointerType === undefined) { this.pointerType = "mouse"; }
            if (this.button === undefined) { this.button = 0; }
            if (this.buttons === undefined) { this.buttons = 0; }
            if (this.clientX === undefined) { this.clientX = 0; }
            if (this.clientY === undefined) { this.clientY = 0; }
            if (this.movementX === undefined) { this.movementX = 0; }
            if (this.movementY === undefined) { this.movementY = 0; }
            this.target = null;
            this.defaultPrevented = false;
            this.preventDefault = function () { this.defaultPrevented = true; };
            this.stopPropagation = function () { };
        };
    }

    document = {
        createElement: function (type) {
            if (type === "canvas") {
                // Hand back a real Canvas polyfill instance rather than the stub above: callers
                // such as FlowMap.FromUrlAsync need a 2D context that actually implements
                // drawImage/getImageData, which the stub does not.
                return engine.createCanvas(64, 64);
            }
            return {};
        },
        removeEventListener: function () { }
    }

    const xhr = new XMLHttpRequest();
    xhr.open("GET", "app:///Scripts/config.json", true);

    xhr.addEventListener("readystatechange", function () {
        if (xhr.status === 200) {
            config = JSON.parse(xhr.responseText);

            if (listTests) {
                // Canonical TSV: index<TAB>title<TAB>referenceImage<TAB>exclusionReason.
                // exclusionReason reflects config state (ignores --include-excluded)
                // so the listing is the same regardless of run flags.
                for (let i = 0; i < config.tests.length; ++i) {
                    const t = config.tests[i];
                    const reason = getExclusionReason(t) || "";
                    console.log(i + "\t" + (t.title || "") + "\t" + (t.referenceImage || "") + "\t" + reason);
                }
                engine.dispose();
                TestUtils.exit(0);
                return;
            }

            // Run tests
            const recursiveRunTest = function (i) {
                // Skip filtered-out tests cheaply (don't count toward --once
                // and don't re-init the engine).
                //
                // Skipped tests (excludeFromAutomaticTesting / onlyVisual /
                // excludedGraphicsApis) are logged loudly when a filter is
                // active so the user sees that --test "X" matched but was
                // skipped. Filter mismatches stay silent to avoid noise on
                // unfiltered runs.
                while (i < config.tests.length) {
                    const t = config.tests[i];
                    const matchesFilter = shouldRunTest(t, i);
                    if (!matchesFilter) {
                        i++;
                        continue;
                    }
                    const reason = getSkipReason(t);
                    if (reason !== null) {
                        console.log("Skipping '" + (t.title || "(unnamed)") + "' -- " + reason);
                        skippedCount++;
                        i++;
                        continue;
                    }
                    break;
                }
                if (i >= config.tests.length) {
                    logRunSummary();
                    engine.dispose();
                    TestUtils.exit(failedCount > 0 ? -1 : 0);
                    return;
                }
                const currentTitle = config.tests[i].title || "(unnamed)";
                runTest(i, function (status) {
                    ranCount++;
                    if (!status) {
                        failedCount++;
                        failedTitles.push(currentTitle);
                        // failTest() already triggered the debugger before
                        // reaching this callback; no second `debugger` here.
                        if (stopOnFirstFailure) {
                            logRunSummary();
                            TestUtils.exit(-1);
                            return;
                        }
                    } else {
                        passedCount++;
                    }
                    i++;
                    if (justOnce || i >= config.tests.length) {
                        logRunSummary();
                        engine.dispose();
                        TestUtils.exit(failedCount > 0 ? -1 : 0);
                        return;
                    }
                    // Defer next iteration to avoid blowing Chakra's
                    // recursion stack on long test lists.
                    setTimeout(function () { recursiveRunTest(i); }, 0);
                });
            }

            recursiveRunTest(0);
        }
    }, false);


    BABYLON.Tools.LoadFile("https://raw.githubusercontent.com/CedricGuillemet/dump/master/droidsans.ttf", (data) => {
        _native.Canvas.loadTTFAsync("droidsans", data).then(function () {
            _native.RootUrl = "https://playground.babylonjs.com";
            console.log("Starting");
            TestUtils.setTitle("Starting Native Validation Tests");
            TestUtils.updateSize(testWidth, testHeight);
            xhr.send();
        });
    }, undefined, undefined, true);
})();
