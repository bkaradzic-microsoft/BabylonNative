import { Constants } from "@babylonjs/core/Engines/constants";
import type { Effect } from "@babylonjs/core/Materials/effect";
import { Color4 } from "@babylonjs/core/Maths/math.color";
import { Vector3 } from "@babylonjs/core/Maths/math.vector";
import { RawTexture } from "@babylonjs/core/Materials/Textures/rawTexture";
import { GPUParticleSystem } from "@babylonjs/core/Particles/gpuParticleSystem";
import "@babylonjs/core/Particles/webgl2ParticleSystem";
import type { Scene } from "@babylonjs/core/scene";

const ParticleTextureSize = 16;

/**
 * Replaces Math.random with a seeded generator so the GPU particle system's random data,
 * and therefore the rendered particles, are identical on every run. The particle system
 * generates its random data lazily while becoming ready and rendering, so the generator
 * stays installed for the rest of the run.
 */
export function seedRandom(): void {
    let seed = 0x2f6b9a1d;
    Math.random = () => {
        seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
        return seed / 0x100000000;
    };
}

function createParticleTexture(scene: Scene): RawTexture {
    const data = new Uint8Array(ParticleTextureSize * ParticleTextureSize * 4);
    const center = (ParticleTextureSize - 1) / 2;
    for (let y = 0; y < ParticleTextureSize; y++) {
        for (let x = 0; x < ParticleTextureSize; x++) {
            const distance = Math.hypot(x - center, y - center) / center;
            const alpha = Math.max(0, 1 - distance);
            const offset = (y * ParticleTextureSize + x) * 4;
            data[offset] = 255;
            data[offset + 1] = 255;
            data[offset + 2] = 255;
            data[offset + 3] = Math.round(alpha * 255);
        }
    }

    return RawTexture.CreateRGBATexture(data, ParticleTextureSize, ParticleTextureSize, scene, false, false, Constants.TEXTURE_NEAREST_SAMPLINGMODE);
}

/**
 * Creates a GPU particle system. GPUParticleSystem selects WebGL2ParticleSystem, which
 * updates the particles with the gpuUpdateParticles transform feedback program; Babylon
 * Native runs that program as a compute shader. The particles are drawn with the
 * instanced gpuRenderParticles vertex/fragment shaders.
 *
 * Every setting that affects shader defines must be identical when preparing shaders
 * and when rendering, otherwise the precompiled sources will not match.
 */
export function createParticleSystem(scene: Scene): GPUParticleSystem {
    const particleSystem = new GPUParticleSystem("particles", { capacity: 64, randomTextureSize: 64 }, scene);
    particleSystem.particleTexture = createParticleTexture(scene);
    particleSystem.emitter = new Vector3(0, 0, -0.6);
    particleSystem.createBoxEmitter(Vector3.Zero(), Vector3.Zero(), new Vector3(-0.9, -0.9, 0), new Vector3(0.9, 0.9, 0));
    particleSystem.color1 = new Color4(1, 0.5, 0.1, 1);
    particleSystem.color2 = new Color4(1, 0.5, 0.1, 1);
    particleSystem.colorDead = new Color4(1, 0.5, 0.1, 1);
    particleSystem.minSize = particleSystem.maxSize = 0.08;
    particleSystem.minLifeTime = particleSystem.maxLifeTime = 1000;
    particleSystem.minEmitPower = particleSystem.maxEmitPower = 0;
    particleSystem.emitRate = 0;
    particleSystem.manualEmitCount = 64;
    particleSystem.blendMode = GPUParticleSystem.BLENDMODE_STANDARD;
    return particleSystem;
}

/**
 * Waits until the particle system is ready. Shader compilation errors are reported on the
 * effects rather than thrown, so a shader missing from the precompiled cache would
 * otherwise only leave the particle system not ready forever.
 */
export async function waitForParticleSystemAsync(scene: Scene, particleSystem: GPUParticleSystem, timeoutMs = 10000): Promise<void> {
    const start = Date.now();
    while (!particleSystem.isReady()) {
        const compiledEffects: Record<string, Effect> = (scene.getEngine() as any)._compiledEffects ?? {};
        for (const name in compiledEffects) {
            const error = compiledEffects[name]!.getCompilationError();
            if (error) {
                throw new Error(`Shader ${name} failed: ${error}`);
            }
        }
        if (Date.now() - start > timeoutMs) {
            throw new Error("GPU particle system did not become ready");
        }
        await new Promise((resolve) => setTimeout(resolve, 0));
    }
}