import * as Mocha from "mocha";
import { expect } from "chai";
import { Constants, NativeEngine, RawTexture, Scene, Texture } from "@babylonjs/core";

const fixtures = [
    {
        name: "RGBA",
        png: "iVBORw0KGgoAAAANSUhEUgAAAAQAAAABEAYAAACprNOOAAAAI0lEQVR4nGOo/38v9Ene//8MDP//NzBAeAwMIB4D4/9/90IBNYURsvcM43UAAAAASUVORK5CYII=",
        pixels: [128, 222, 227, 255, 0, 255, 128, 128, 222, 227, 0, 0, 255, 0, 255, 222],
    },
    {
        name: "RGB",
        png: "iVBORw0KGgoAAAANSUhEUgAAAAQAAAABEAIAAAAmzkTZAAAAG0lEQVR4nGOo/38v9EkeA8P//w0MMBYD4/9/AKWbDQOAUd17AAAAAElFTkSuQmCC",
        pixels: [128, 222, 227, 255, 0, 255, 128, 255, 222, 227, 0, 255, 255, 0, 255, 255],
    },
    {
        name: "grayscale",
        png: "iVBORw0KGgoAAAANSUhEUgAAAAQAAAABEAAAAACMx4xSAAAAEUlEQVR4nGOo/38v9Ene//8AGa4GAvbhooAAAAAASUVORK5CYII=",
        pixels: [128, 128, 128, 255, 222, 222, 222, 255, 227, 227, 227, 255, 255, 255, 255, 255],
    },
    {
        name: "grayscale with alpha",
        png: "iVBORw0KGgoAAAANSUhEUgAAAAQAAAABEAQAAAADpRsFAAAAF0lEQVR4nGOo/////73Q+v9P8hgYQCwAYP8KsdIXi8oAAAAASUVORK5CYII=",
        pixels: [128, 128, 128, 255, 222, 222, 222, 128, 227, 227, 227, 0, 255, 255, 255, 222],
    },
];

export function registerPng16Tests(
    describe: typeof Mocha.describe,
    it: typeof Mocha.it,
    skipCanvasGpuTests: boolean
): void {
    describe("NativeEngine 16-bit PNG decoding", function () {
        this.timeout(10000);
        const test = skipCanvasGpuTests ? it.skip : it;
        for (const fixture of fixtures) {
            for (const generateMips of [false, true]) {
                test(`uploads ${fixture.name} PNG as browser RGBA8 (mips ${generateMips})`, async function () {
                    const engine = new NativeEngine();
                    const scene = new Scene(engine);
                    try {
                        const texture = await new Promise<Texture>((resolve, reject) => {
                            const image = new Texture(
                                "data:image/png;base64," + fixture.png, scene, !generateMips, false,
                                Constants.TEXTURE_NEAREST_SAMPLINGMODE, () => resolve(image),
                                message => reject(new Error(message || "PNG texture load failed"))
                            );
                        });
                        const pixels = await texture.readPixels();
                        if (!(pixels instanceof Uint8Array)) {
                            throw new Error("Expected unsigned-byte PNG texture readback");
                        }
                        expect(Array.from(pixels)).to.deep.equal(fixture.pixels);
                        if (generateMips) {
                            const mip = await texture.readPixels(0, 1);
                            expect(mip instanceof Uint8Array).to.equal(true);
                            expect(mip?.byteLength).to.equal(8);
                        }
                    } finally {
                        scene.dispose();
                        engine.dispose();
                    }
                });
            }

            test(`decodes ${fixture.name} PNG through Canvas images`, async function () {
                const engine = new NativeEngine();
                try {
                    const bitmap = await engine._createImageBitmapFromSource("data:image/png;base64," + fixture.png);
                    expect(bitmap.width).to.equal(4);
                    expect(bitmap.height).to.equal(1);
                    const pixels = engine.resizeImageBitmap(bitmap, 4, 1);
                    expect(Array.from(pixels)).to.deep.equal(fixture.pixels);
                } finally {
                    engine.dispose();
                }
            });
        }

        test("preserves explicitly uploaded floating-point texture values", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const data = new Float32Array([-0.25, 0.49999237048905165, 2, 1]);
                const texture = RawTexture.CreateRGBATexture(
                    data, 1, 1, scene, false, false, Constants.TEXTURE_NEAREST_SAMPLINGMODE,
                    Constants.TEXTURETYPE_FLOAT
                );
                const pixels = await texture.readPixels();
                if (!(pixels instanceof Float32Array)) {
                    throw new Error("Expected floating-point raw texture readback");
                }
                expect(Array.from(pixels)).to.deep.equal(Array.from(data));
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });
    });
}
