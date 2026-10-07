# NativeDraco

> ⚠️ **This plugin is experimental and subject to change.**

The NativeDraco plugin provides native [Draco](https://github.com/google/draco) geometry compression and decompression to Babylon, so `KHR_draco_mesh_compression` glTF assets can be decoded — and meshes encoded — without shipping and instantiating the Draco WebAssembly modules.

The shotgun support branch enables the plugin by default. Upstream remains opt-in with `-D BABYLON_NATIVE_PLUGIN_NATIVEDRACO=ON`.

Codec-only builds skip Draco's unused third-party submodules. Setting `DRACO_TRANSCODER_SUPPORTED=ON` also fetches the transcoder's Eigen, filesystem, and tinygltf dependencies.

## Limitations

- **Full bitstream.** The support branch uses `DRACO_GLTF_BITSTREAM=OFF` for older and non-glTF validation streams. This increases binary size and can emit encoded features unsupported by a glTF-only decoder. The code-only branch retains upstream's glTF subset.
- **Paired Babylon.js runtime.** The full shotgun catalog uses the matching protocol-10 Babylon.js fork, including its native codec probes. NativeDraco's unit tests also exercise both export forms directly.

## Design

The versioned API is exposed as a `DracoCodec` object on the `_native` global. `Initialize` also publishes `decodeDracoMesh` and `encodeDracoMesh`, backed by the same implementations as `DracoCodec.Decode` and `DracoCodec.Encode`, for compatibility with Babylon.js native codec feature probes. The grouped API is retained so that:

1. **One feature probe.** JavaScript checks for the object once instead of once per entry point.
2. **The object can carry a version.** Draco's bitstream is versioned; a caller holding a stream this build is too old to read otherwise has no way to find out ahead of time. A bare function name cannot express that.

`VertexDataTypedArray` below matches Babylon.js (`Exclude<TypedArray, Float64Array | BigInt64Array | BigUint64Array>`). Prefer that over bare `ArrayBufferView`, which also includes `DataView` and BigInt typed arrays this implementation rejects.

```typescript
// Matches Babylon.js core/Buffers/bufferUtils VertexDataTypedArray.
type VertexDataTypedArray = Exclude<TypedArray, Float64Array | BigInt64Array | BigUint64Array>;

interface INative {
  DracoCodec: {
    Decode: (
      data: VertexDataTypedArray,
      attributeIds?: { [kind: string]: number }
    ) => {
      indices: Uint32Array | null;
      attributes: Array<{
        kind: string;
        data: Float32Array | Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array;
        size: number;
        byteOffset: number;
        byteStride: number;
        normalized: boolean;
      }>;
      totalVertices: number;
    };
    Encode: (
      attributes: Array<{
        kind: string;
        dracoName: string;
        size: number;
        data: VertexDataTypedArray;
      }>,
      indices?: Uint16Array | Uint32Array,
      options?: {
        method?: "MESH_EDGEBREAKER_ENCODING" | "MESH_SEQUENTIAL_ENCODING";
        quantizationBits?: { [kind: string]: number };
        encodeSpeed?: number;
        decodeSpeed?: number;
      }
    ) => {
      data: Int8Array;
      attributeIds: { [kind: string]: number };
    };
    Version: string;
  };
  decodeDracoMesh: INative["DracoCodec"]["Decode"];
  encodeDracoMesh: INative["DracoCodec"]["Encode"];
}
```

`attributeIds` is the glTF path: a map of Babylon vertex-buffer kind to Draco unique attribute id, taken from the `KHR_draco_mesh_compression` extension. When it is omitted, attributes are identified by their Draco geometry type instead.

`indices` is `null` for geometry that decodes to a point cloud rather than a triangular mesh.

`Encode` requires a `POSITION` attribute. Omitting `indices` treats the vertices as a flat triangle list, so the vertex count must then be a multiple of three. It returns the `attributeIds` map the corresponding `Decode` call needs, and `data` as an `Int8Array` — matching Babylon.js's `IDracoEncodedMeshData`, which is typed that way because the WASM encoder returns a view onto emscripten's signed `HEAP8`.

## Notes

`Decode` reads the keys of `attributeIds` via `Object.keys` obtained from the global object, rather than `Napi::Object::GetPropertyNames()`. `GetPropertyNames()` is unusable on JavaScriptCore — the default engine on macOS and iOS — and the enumeration semantics differ across the non-V8 backends generally. See the comment in `Source/NativeDraco.cpp` and [JsRuntimeHost#216](https://github.com/BabylonJS/JsRuntimeHost/issues/216).
