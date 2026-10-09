/// <reference types="node" />
import { NativeEngine } from "@babylonjs/core/Engines/nativeEngine";
import { ThinNativeEngine } from "@babylonjs/core/Engines/thinNativeEngine";
import fs from "fs";
import { AllEffects } from "../src/effects/effectList";

let effectName: string = "unknown";

declare const global: {
    window: Object;
    _native: any;
};

type CapturedProgram = {
    effect: string;
    vertex: string;
    fragment: string;
    varyings?: string[];
    attributes: string[];
    instancedAttributes: Set<string>;
};

const programs: CapturedProgram[] = [];

// recordVertexBuffer only receives an attribute location and a divisor, so the locations handed out
// by getAttributes encode programIndex * LocationStride + attributeIndex. That maps an instanced
// location back to its program and attribute name, which ShaderTool needs (-i) to precompile the
// instanced variant. Any stride above a program's attribute count works.
const LocationStride = 1024;

/**
 * Mock the global objects that the NativeEngine expects to find when
 * running in a Native environment.
 *
 * When an Effect is created, Babylon calls `createProgram` on the engine.
 * Our mock intercepts this call and records the final preprocessed vertex and
 * fragment shader source so that ShaderTool can compile them offline. Programs
 * created with transform feedback varyings are recorded with their varyings, and
 * attributes recorded into vertex arrays with an instance divisor are recorded so
 * that the instanced variant of the program can be precompiled as well.
 * Every other native call is stubbed so that real Babylon features (for example
 * GPU particles) can run far enough to create and draw with their shaders.
 */
function createMockNativeLayer(): void {
    global.window = {};

    class MockEngine {
        createProgram(vertex: string, fragment: string, varyings?: string[]) {
            programs.push({ effect: effectName, vertex, fragment, varyings, attributes: [], instancedAttributes: new Set() });
            return new Uint32Array([programs.length - 1]);
        }
        getUniforms() {
            return [];
        }
        getAttributes(program: Uint32Array, names: string[]) {
            const programIndex = program[0]!;
            const captured = programs[programIndex]!;
            return names.map((name) => {
                let index = captured.attributes.indexOf(name);
                if (index < 0) {
                    index = captured.attributes.push(name) - 1;
                }
                return programIndex * LocationStride + index;
            });
        }
        recordVertexBuffer(_vertexArray: unknown, _buffer: unknown, location: number, _byteOffset: number, _byteStride: number, _numElements: number, _type: number, _normalized: boolean, divisor: number) {
            if (divisor > 0) {
                const captured = programs[Math.floor(location / LocationStride)]!;
                captured.instancedAttributes.add(captured.attributes[location % LocationStride]!);
            }
        }
        getRenderWidth() {
            return 1;
        }
        getRenderHeight() {
            return 1;
        }
    }

    // Any other engine method or constant is a stub. Commands and native handles are
    // written to the command stream as Uint32Arrays; nothing executes them.
    const stub = () => new Uint32Array(1);
    const engineInstanceHandler: ProxyHandler<MockEngine> = {
        get: (target, property, receiver) => (property in target ? Reflect.get(target, property, receiver) : stub),
    };
    const engineClass = new Proxy(MockEngine, {
        construct: (target, args) => new Proxy(Reflect.construct(target, args), engineInstanceHandler),
        get: (target, property, receiver) => {
            if (property in target) {
                return Reflect.get(target, property, receiver);
            }
            return typeof property === "string" && property.startsWith("COMMAND_") ? new Uint32Array(1) : 0;
        },
    });

    (MockEngine as any).PROTOCOL_VERSION = (ThinNativeEngine as any).PROTOCOL_VERSION;

    global._native = {
        Engine: engineClass,
        NativeDataStream: class {
            static VALIDATION_ENABLED = false;
        },
    };
}

function writeFile(fileName: string, contents: string): void {
    console.log(`Writing ${fileName}`);
    fs.writeFileSync(fileName, contents);
}

/**
 * Write the captured programs under dist/shaders/<name>/:
 * - vertex.fx / fragment.fx for the effect's program, plus instanced.txt listing the
 *   attributes it reads per instance (empty when it is not instanced);
 * - transformFeedback.fx / transformFeedback.txt for a transform feedback program and its
 *   captured varyings.
 */
function saveShaderFiles(): void {
    for (const program of programs) {
        const dir = `./dist/shaders/${program.effect}`;
        fs.mkdirSync(dir, { recursive: true });

        if (program.varyings) {
            if (fs.existsSync(`${dir}/transformFeedback.fx`)) {
                throw new Error(`Effect ${program.effect} created more than one transform feedback program`);
            }
            writeFile(`${dir}/transformFeedback.fx`, program.vertex);
            writeFile(`${dir}/transformFeedback.txt`, program.varyings.join(","));
        } else {
            if (fs.existsSync(`${dir}/vertex.fx`)) {
                throw new Error(`Effect ${program.effect} created more than one program`);
            }
            writeFile(`${dir}/vertex.fx`, program.vertex);
            writeFile(`${dir}/fragment.fx`, program.fragment);
            writeFile(`${dir}/instanced.txt`, [...program.instancedAttributes].join(","));
        }
    }
}

/**
 * Prepare all registered Effects for offline compilation.
 *
 * Creates a mocked NativeEngine and runs each effect through it, which
 * causes the final preprocessed shader source to be captured and written to
 * dist/shaders.
 */
async function main(): Promise<void> {
    createMockNativeLayer();
    fs.rmSync("./dist/shaders", { recursive: true, force: true });

    const engine = new NativeEngine();
    engine.getCaps().parallelShaderCompile = undefined;

    for (const effect of AllEffects) {
        effectName = effect.name;
        console.log(`Preparing ${effectName} effect...`);
        await effect.create(engine);
    }

    saveShaderFiles();
}

main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});