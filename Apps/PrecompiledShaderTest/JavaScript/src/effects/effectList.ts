import type { AbstractEngine } from "@babylonjs/core/Engines/abstractEngine";
import { FreeCamera } from "@babylonjs/core/Cameras/freeCamera";
import { Vector3 } from "@babylonjs/core/Maths/math.vector";
import { Scene } from "@babylonjs/core/scene";
import { createParticleSystem } from "./particleEffect";
import { createSimpleEffect } from "./simpleEffect";

type EffectInfo = {
    name: string;
    create: (engine: AbstractEngine) => void | Promise<void>;
};

export const AllEffects: EffectInfo[] = [
    {
        name: "simple",
        create: (engine) => {
            createSimpleEffect(engine);
        },
    },
    {
        // GPU particles: the gpuUpdateParticles transform feedback program and the
        // instanced gpuRenderParticles vertex/fragment shaders.
        name: "particles",
        create: async (engine) => {
            const scene = new Scene(engine);
            new FreeCamera("camera", Vector3.Zero(), scene);
            const particleSystem = createParticleSystem(scene);
            particleSystem.start();
            await scene.whenReadyAsync();
            // Rendering records the particle vertex arrays, which identifies the attributes
            // the render program reads per instance.
            scene.render();
        },
    },
];