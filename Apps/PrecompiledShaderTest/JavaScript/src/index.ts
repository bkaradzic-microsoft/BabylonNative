import { NativeTexture } from "@babylonjs/core/Engines/Native/nativeInterfaces";
import { NativeEngine } from "@babylonjs/core/Engines/nativeEngine";
import { RenderTargetTexture } from "@babylonjs/core/Materials/Textures/renderTargetTexture";
import { ShaderMaterial } from "@babylonjs/core/Materials/shaderMaterial";
import { Color4 } from "@babylonjs/core/Maths/math.color";
import { MeshBuilder } from "@babylonjs/core/Meshes/meshBuilder";
import { Scene } from "@babylonjs/core/scene";
import { createParticleSystem, seedRandom, waitForParticleSystemAsync } from "./effects/particleEffect";
import { fragmentShader, vertexShader } from "./effects/simpleEffect";
import { Vector3 } from "@babylonjs/core/Maths/math.vector";
import { FreeCamera } from "@babylonjs/core/Cameras/freeCamera";

let engine: NativeEngine;
let scene: Scene;
let outputTexture: RenderTargetTexture;

function startup(
    nativeTexture: NativeTexture,
    width: number,
    height: number
): void {
    engine = new NativeEngine();
    scene = new Scene(engine);

    outputTexture = new RenderTargetTexture(
        "outputTexture",
        { width, height },
        scene,
        {
            colorAttachment: engine.wrapNativeTexture(nativeTexture),
            generateDepthBuffer: true,
            generateStencilBuffer: true,
        }
    );
}

async function renderSceneAsync(): Promise<void> {
    const camera = new FreeCamera("camera", new Vector3(0, 0, -2.5), scene);
    camera.setTarget(Vector3.Zero());
    camera.outputRenderTarget = outputTexture;

    const material = new ShaderMaterial(
        "shaderMaterial",
        scene,
        { vertexSource: vertexShader, fragmentSource: fragmentShader },
        {
            attributes: ["position", "normal"],
            uniforms: ["world", "viewProjection", "baseColor"],
        }
    );
    material.setColor4("baseColor", new Color4(0.4, 0.6, 1.0, 1.0));

    const sphere = MeshBuilder.CreateSphere(
        "sphere",
        { segments: 32, diameter: 0.9 },
        scene
    );
    sphere.material = material;

    // GPU particles update through a transform feedback program, which Babylon Native
    // runs as a compute shader that only exists here if it was precompiled into the cache.
    // Particle random data is seeded so the rendered particles match the reference image.
    seedRandom();
    const particleSystem = createParticleSystem(scene);
    particleSystem.start();

    // Scene readiness includes the particle system, so wait for it first and fail
    // instead of waiting forever when one of its shaders is missing from the cache.
    await waitForParticleSystemAsync(scene, particleSystem);
    await scene.whenReadyAsync();

    // Babylon.js GPU particles ping-pong their buffers and draw the state produced by the previous
    // frame's update (as in browsers), so the particles first appear in the second frame.
    scene.render();
    scene.render();
}

declare const globalThis: {
    startup: typeof startup;
    renderSceneAsync: typeof renderSceneAsync;
};

globalThis.startup = startup;
globalThis.renderSceneAsync = renderSceneAsync;
