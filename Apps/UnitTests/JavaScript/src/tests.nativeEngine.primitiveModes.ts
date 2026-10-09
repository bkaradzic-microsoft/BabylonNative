import * as Mocha from "mocha";
import { expect } from "chai";
import {
    Color4,
    FreeCamera,
    Material,
    Matrix,
    Mesh,
    NativeEngine,
    Quaternion,
    RenderTargetTexture,
    Scene,
    ShaderMaterial,
    SubMesh,
    Vector3,
    VertexBuffer,
} from "@babylonjs/core";

type IndexFormat = "none" | "uint16" | "uint32";

async function comparePrimitiveExpansion(mode: number, indexFormat: IndexFormat, instanced: boolean): Promise<void> {
    const indexed = indexFormat !== "none";
    const IndexArray = indexFormat === "uint32" ? Uint32Array : Uint16Array;
    const engine = new NativeEngine();
    const scene = new Scene(engine);
    try {
        const camera = new FreeCamera("camera", new Vector3(0, 0, -3), scene);
        camera.setTarget(Vector3.Zero());
        const material = new ShaderMaterial("primitive", scene, {
            vertexSource: `
                precision highp float;
                attribute vec3 position;
                #include<instancesDeclaration>
                varying vec3 vColor;
                void main() {
                    #include<instancesVertex>
                    gl_Position = finalWorld * vec4(position, 1.0);
                    vColor = vec3(position.xy * 0.5 + 0.5, 1.0);
                }`,
            fragmentSource: `
                precision highp float;
                varying vec3 vColor;
                void main() { gl_FragColor = vec4(vColor, 1.0); }`,
        }, { attributes: ["position"], uniforms: ["world"] });
        material.backFaceCulling = false;
        material.fillMode = mode;
        const positions = [-0.75, -0.75, 0, 0.75, -0.75, 0, 0.75, 0.75, 0, -0.75, 0.75, 0];
        const actual = new Mesh("actual", scene);
        actual.setVerticesData(VertexBuffer.PositionKind, indexed ? positions : [9, 9, 0, 9, 9, 0, ...positions, 9, 9, 0]);
        actual.material = material;
        if (indexed) {
            actual.setIndices(new IndexArray([99, 99, 0, 1, 2, 3, 99]), null, true);
        } else {
            actual.isUnIndexed = true;
        }
        actual.releaseSubMeshes();
        new SubMesh(0, indexed ? 0 : 2, 4, indexed ? 2 : 0, indexed ? 4 : 0, actual);

        const expectedMaterial = material.clone("expandedMaterial");
        if (!expectedMaterial) {
            throw new Error("Failed to clone primitive material");
        }
        expectedMaterial.fillMode = mode === Material.LineLoopDrawMode ? Material.LineListDrawMode : Material.TriangleFillMode;
        const expected = new Mesh("expected", scene);
        expected.setVerticesData(VertexBuffer.PositionKind, positions);
        expected.material = expectedMaterial;
        const expandedIndices = (order: number[]): number[] => mode === Material.LineLoopDrawMode
            ? [order[0], order[1], order[1], order[2], order[2], order[3], order[3], order[0]]
            : [order[0], order[1], order[2], order[0], order[2], order[3]];
        expected.setIndices(expandedIndices([0, 1, 2, 3]), null, true);
        if (instanced) {
            const matrices = new Float32Array(32);
            Matrix.Compose(new Vector3(0.4, 0.7, 1), Quaternion.Identity(), new Vector3(-0.45, 0, 0)).copyToArray(matrices, 0);
            Matrix.Compose(new Vector3(0.4, 0.7, 1), Quaternion.Identity(), new Vector3(0.45, 0, 0)).copyToArray(matrices, 16);
            actual.thinInstanceSetBuffer("matrix", matrices, 16, true);
            expected.thinInstanceSetBuffer("matrix", matrices, 16, true);
        }
        const target = new RenderTargetTexture("primitiveTarget", { width: 32, height: 32 }, scene, {
            generateDepthBuffer: false,
            generateStencilBuffer: false,
            samples: 1,
        });
        target.activeCamera = camera;
        target.clearColor = new Color4(0, 0, 0, 1);
        await scene.whenReadyAsync();
        const render = async (mesh: Mesh): Promise<Uint8Array> => {
            target.renderList = [mesh];
            target.render();
            const pixels = await target.readPixels();
            if (!(pixels instanceof Uint8Array)) {
                throw new Error("Expected RGBA8 primitive readback");
            }
            return pixels.slice();
        };
        const check = async (): Promise<Uint8Array> => {
            const actualPixels = await render(actual);
            const expectedPixels = await render(expected);
            expect(expectedPixels.some((value, index) => index % 4 === 2 && value === 255), "control must draw visible geometry").to.equal(true);
            expect(Array.from(actualPixels), "expanded primitive must match explicit list").to.deep.equal(Array.from(expectedPixels));
            return actualPixels;
        };
        const before = await check();
        if (indexed) {
            actual.updateIndices(new IndexArray([99, 99, 0, 2, 1, 3, 99]));
            expected.updateIndices(expandedIndices([0, 2, 1, 3]));
            const after = await check();
            expect(Array.from(after), "index update must change the rendered geometry").not.to.deep.equal(Array.from(before));
        }
    } finally {
        scene.dispose();
        engine.dispose();
    }
}

export function registerPrimitiveModeTests(
    describe: typeof Mocha.describe,
    it: typeof Mocha.it,
    skipCanvasGpuTests: boolean
): void {
    describe("NativeEngine expanded primitive modes", function () {
        this.timeout(10000);
        for (const mode of [Material.LineLoopDrawMode, Material.TriangleFanDrawMode]) {
            for (const indexFormat of ["none", "uint16", "uint32"] as const) {
                for (const instanced of [false, true]) {
                    (skipCanvasGpuTests ? it.skip : it)(`mode=${mode} indices=${indexFormat} instanced=${instanced} preserves draw ranges and topology`, async function () {
                        await comparePrimitiveExpansion(mode, indexFormat, instanced);
                    });
                }
            }
        }
    });
}
