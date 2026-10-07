import * as Mocha from "mocha";
import { expect } from "chai";
import { Constants, DynamicTexture, FreeCamera, NativeEngine, Scene, Vector3 } from "@babylonjs/core";

type CanvasContext = ReturnType<DynamicTexture["getContext"]>;

const opaqueOrangePng = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFUlEQVR4nGP838DwnwEJMCFziBMAAKTRAobDsKmdAAAAAElFTkSuQmCC";

function fillImageData(context: CanvasContext, width: number, height: number, pixel: (x: number, y: number) => readonly number[], dx = 0, dy = 0): void {
    const imageData = context.getImageData(0, 0, width, height);
    for (let y = 0; y < height; ++y) {
        for (let x = 0; x < width; ++x) {
            imageData.data.set(pixel(x, y), (y * width + x) * 4);
        }
    }
    context.putImageData(imageData, dx, dy);
}

async function readTexture(texture: DynamicTexture, premulAlpha = false): Promise<Uint8Array> {
    texture.update(false, premulAlpha);
    const pixels = await texture.readPixels();
    if (!(pixels instanceof Uint8Array)) {
        throw new Error("Expected RGBA8 GPU readback for the canvas texture");
    }
    // Native readback uses WebGL's bottom-up row order; assertions use Canvas coordinates.
    const { width, height } = texture.getSize();
    const stride = width * 4;
    const topDown = new Uint8Array(pixels.length);
    for (let y = 0; y < height; ++y) {
        topDown.set(pixels.subarray(y * stride, (y + 1) * stride), (height - 1 - y) * stride);
    }
    return topDown;
}

export function registerCanvasImageTests(
    describe: typeof Mocha.describe,
    it: typeof Mocha.it,
    skipCanvasGpuTests: boolean
): void {
    describe("NativeEngine Canvas pixel-backed images", function () {
        this.timeout(10000);
        const test = skipCanvasGpuTests ? it.skip : it;

        test("keeps queued alpha and mipmap representations independent", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                new FreeCamera("camera", new Vector3(0, 0, -1), scene);
                const canvas = engine.createCanvas(8, 8);
                const context = canvas.getContext("2d");
                context.fillStyle = "#FF000066";
                context.fillRect(0, 0, 8, 8);
                const targets = [
                    { premulAlpha: false, mipmaps: false },
                    { premulAlpha: false, mipmaps: true },
                    { premulAlpha: true, mipmaps: true },
                    { premulAlpha: true, mipmaps: false },
                ].map(options => ({
                    ...options,
                    texture: new DynamicTexture("queued Canvas copy", { width: 8, height: 8 }, scene, options.mipmaps, Constants.TEXTURE_NEAREST_SAMPLINGMODE),
                }));
                scene.onBeforeRenderObservable.addOnce(() => {
                    for (const { texture, premulAlpha } of targets) {
                        engine.updateDynamicTexture(texture.getInternalTexture(), canvas, false, premulAlpha);
                    }
                });
                scene.render();
                for (const { texture, premulAlpha } of targets) {
                    const pixels = await texture.readPixels();
                    if (!(pixels instanceof Uint8Array)) {
                        throw new Error("Expected RGBA8 Canvas readback");
                    }
                    expect(Array.from(pixels.subarray(0, 4))).to.deep.equal([premulAlpha ? 102 : 255, 0, 0, 102]);
                }
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("copies regenerated Canvas mipmaps in the requested alpha representation", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const texture = new DynamicTexture("Canvas mip chain", { width: 8, height: 8 }, scene, true, Constants.TEXTURE_TRILINEAR_SAMPLINGMODE);
                if (!texture.getInternalTexture()?.generateMipMaps) {
                    this.skip();
                }
                const context = texture.getContext();
                for (const premulAlpha of [false, true]) {
                    for (const blue of [true, false]) {
                        context.clearRect(0, 0, 8, 8);
                        fillImageData(context, 8, 8, (x, y) => (
                            (x + y) % 2 === 0 ? [255, 0, 0, 102] : blue ? [0, 0, 255, 102] : [0, 255, 0, 102]
                        ));
                        texture.update(false, premulAlpha);
                        for (const mip of [1, 2, 3]) {
                            const pixels = await texture.readPixels(0, mip);
                            if (!(pixels instanceof Uint8Array)) {
                                throw new Error("Expected RGBA8 Canvas mip readback");
                            }
                            const rgba = pixels;
                            const size = 8 >> mip;
                            expect(rgba.length).to.equal(size * size * 4);
                            const half = premulAlpha ? 51 : 128;
                            const expected = blue ? [half, 0, half, 102] : [half, half, 0, 102];
                            for (let index = 0; index < rgba.length; ++index) {
                                expect(rgba[index], `mip ${mip}, byte ${index}`).to.be.closeTo(expected[index % 4], 1);
                            }
                        }
                    }
                }
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("interpolates translucent gradient stops as straight color", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const texture = new DynamicTexture("Canvas gradient alpha", { width: 100, height: 100 }, scene, false, Constants.TEXTURE_NEAREST_SAMPLINGMODE);
                const context = texture.getContext();
                for (const radial of [false, true]) {
                    context.clearRect(0, 0, 100, 100);
                    const gradient = radial ? context.createRadialGradient(0, 50, 0, 0, 50, 100) : context.createLinearGradient(0, 0, 100, 0);
                    gradient.addColorStop(0, "#FF0000FF");
                    gradient.addColorStop(1, "#00000000");
                    context.fillStyle = gradient;
                    context.fillRect(0, 0, 100, 100);
                    const pixels = await readTexture(texture);
                    const offset = (50 * 100 + 49) * 4;
                    for (const [channel, expected] of [128, 0, 0, 129].entries()) {
                        expect(pixels[offset + channel], `${radial ? "radial" : "linear"} gradient channel ${channel}`).to.be.closeTo(expected, 3);
                    }
                }
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("uploads straight or premultiplied Canvas alpha without changing row order", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const texture = new DynamicTexture(
                    "Canvas alpha representation",
                    { width: 10, height: 8 },
                    scene,
                    false,
                    Constants.TEXTURE_NEAREST_SAMPLINGMODE
                );
                const context = texture.getContext();
                context.fillStyle = "#FF000066";
                context.fillRect(1, 1, 4, 3);
                context.fillStyle = "#00FF00";
                context.fillRect(6, 5, 3, 2);
                context.fillStyle = "#0000FF01";
                context.fillRect(7, 1, 2, 2);

                for (const premulAlpha of [false, true, false]) {
                    const pixels = await readTexture(texture, premulAlpha);
                    const pixel = (x: number, y: number) => Array.from(pixels.subarray((y * 10 + x) * 4, (y * 10 + x + 1) * 4));
                    expect(pixel(2, 2), "translucent red").to.deep.equal([premulAlpha ? 102 : 255, 0, 0, 102]);
                    expect(pixel(7, 6), "opaque green").to.deep.equal([0, 255, 0, 255]);
                    expect(pixel(8, 2), "one-byte alpha").to.deep.equal([0, 0, premulAlpha ? 1 : 255, 1]);
                    expect(pixel(0, 7), "transparent pixels").to.deep.equal([0, 0, 0, 0]);
                    expect(texture.getInternalTexture()?._premulAlpha).to.equal(premulAlpha);
                }

                const source = context.getImageData(2, 2, 1, 1);
                expect(Array.from(source.data), "Canvas retains its original contents").to.deep.equal([255, 0, 0, 102]);
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("refreshes straight-alpha uploads after drawing, clearing, and resizing", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const texture = new DynamicTexture(
                    "Canvas alpha refresh",
                    { width: 8, height: 6 },
                    scene,
                    false,
                    Constants.TEXTURE_NEAREST_SAMPLINGMODE
                );
                for (const size of [{ width: 8, height: 6 }, { width: 12, height: 10 }]) {
                    if (texture.getSize().width !== size.width) {
                        texture.scaleTo(size.width, size.height);
                    }
                    const context = texture.getContext();
                    for (const color of ["#FF000066", "#0000FF66"]) {
                        context.clearRect(0, 0, size.width, size.height);
                        context.fillStyle = color;
                        context.fillRect(2, 2, 3, 2);
                        const pixels = await readTexture(texture);
                        const offset = (2 * size.width + 3) * 4;
                        expect(Array.from(pixels.subarray(offset, offset + 4))).to.deep.equal(
                            color === "#FF000066" ? [255, 0, 0, 102] : [0, 0, 255, 102]
                        );
                    }
                    context.clearRect(0, 0, size.width, size.height);
                    expect(Array.from(await readTexture(texture)).every(value => value === 0)).to.equal(true);
                }
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("keeps putImageData pixels alive through the GPU flush", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const texture = new DynamicTexture(
                    "putImageData lifetime",
                    { width: 10, height: 8 },
                    scene,
                    false,
                    Constants.TEXTURE_NEAREST_SAMPLINGMODE
                );
                fillImageData(texture.getContext(), 4, 3, () => [0, 255, 255, 255], 3, 2);

                const pixels = await readTexture(texture);
                const pixel = (x: number, y: number) => Array.from(pixels.subarray((y * 10 + x) * 4, (y * 10 + x + 1) * 4));
                expect(pixel(4, 3), "inside putImageData rectangle").to.deep.equal([0, 255, 255, 255]);
                expect(pixel(2, 3), "left of putImageData rectangle").to.deep.equal([0, 0, 0, 0]);
                expect(pixel(4, 5), "below putImageData rectangle").to.deep.equal([0, 0, 0, 0]);
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("draws an offscreen canvas at a nonzero destination", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const source = engine.createCanvas(4, 4);
                fillImageData(source.getContext("2d"), 4, 4, () => [255, 0, 255, 255]);

                const texture = new DynamicTexture(
                    "offscreen canvas copy",
                    { width: 12, height: 10 },
                    scene,
                    false,
                    Constants.TEXTURE_NEAREST_SAMPLINGMODE
                );
                texture.getContext().drawImage(source, 3, 2);

                const pixels = await readTexture(texture);
                const pixel = (x: number, y: number) => Array.from(pixels.subarray((y * 12 + x) * 4, (y * 12 + x + 1) * 4));
                expect(pixel(4, 3), "inside copied canvas").to.deep.equal([255, 0, 255, 255]);
                expect(pixel(2, 3), "left of copied canvas").to.deep.equal([0, 0, 0, 0]);
                expect(pixel(4, 6), "below copied canvas").to.deep.equal([0, 0, 0, 0]);
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("draws an ImageBitmap source through the GPU path", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const bitmap = await engine._createImageBitmapFromSource(opaqueOrangePng);
                expect(bitmap.width).to.equal(4);
                expect(bitmap.height).to.equal(4);

                const texture = new DynamicTexture(
                    "ImageBitmap copy",
                    { width: 10, height: 8 },
                    scene,
                    false,
                    Constants.TEXTURE_NEAREST_SAMPLINGMODE
                );
                texture.getContext().drawImage(bitmap, 3, 2);

                const pixels = await readTexture(texture);
                const pixel = (x: number, y: number) => Array.from(pixels.subarray((y * 10 + x) * 4, (y * 10 + x + 1) * 4));
                expect(pixel(4, 3), "inside copied ImageBitmap").to.deep.equal([255, 128, 0, 255]);
                expect(pixel(2, 3), "left of copied ImageBitmap").to.deep.equal([0, 0, 0, 0]);
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });

        test("crops and scales a nonzero offscreen-canvas source rectangle", async function () {
            const engine = new NativeEngine();
            const scene = new Scene(engine);
            try {
                const source = engine.createCanvas(18, 8);
                fillImageData(source.getContext("2d"), 18, 8, (x) => (
                    x < 6 ? [255, 0, 0, 255] :
                    x < 12 ? [0, 255, 0, 255] :
                    [0, 0, 255, 255]
                ));

                const texture = new DynamicTexture(
                    "offscreen canvas crop",
                    { width: 14, height: 12 },
                    scene,
                    false,
                    Constants.TEXTURE_NEAREST_SAMPLINGMODE
                );
                texture.getContext().drawImage(source, 7, 2, 4, 4, 3, 2, 8, 8);

                const pixels = await readTexture(texture);
                const pixel = (x: number, y: number) => Array.from(pixels.subarray((y * 14 + x) * 4, (y * 14 + x + 1) * 4));
                expect(pixel(4, 5), "left interior of scaled crop").to.deep.equal([0, 255, 0, 255]);
                expect(pixel(9, 5), "right interior of scaled crop").to.deep.equal([0, 255, 0, 255]);
                expect(pixel(2, 5), "outside destination rectangle").to.deep.equal([0, 0, 0, 0]);
            } finally {
                scene.dispose();
                engine.dispose();
            }
        });
    });
}
