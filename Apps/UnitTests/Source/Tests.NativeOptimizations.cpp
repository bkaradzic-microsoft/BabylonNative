#include <gtest/gtest.h>
#ifdef HAS_NATIVE_OPTIMIZATIONS
#include <Babylon/AppRuntime.h>
#include <Babylon/Plugins/NativeOptimizations.h>
#include <napi/env.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <stdexcept>
#endif

TEST(NativeOptimizations, VertexTransformsAcceptPreciseMatrixStorage)
{
#ifndef HAS_NATIVE_OPTIMIZATIONS
    GTEST_SKIP() << "NativeOptimizations is disabled";
#else
    std::promise<void> completed;
    auto completion = completed.get_future();
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&](Napi::Env env) {
        try
        {
            Babylon::Plugins::NativeOptimizations::Initialize(env);
            Napi::Eval(env, R"(
                (function () {
                    const values = [1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 16777217, -3, 5, 1];
                    for (const matrix of [new Float32Array(values), new Float64Array(values), values]) {
                        for (const kind of ["Coordinates", "Normals", "Tangents"]) {
                            const stride = kind === "Tangents" ? 4 : 3;
                            const data = new Float32Array(stride * 3).fill(99);
                            data.set([-16777216, 0, 0], stride);
                            const original = data.slice(stride, stride * 2);
                            const expected = new Float32Array(data);
                            const x = data[stride], y = data[stride + 1], z = data[stride + 2];
                            const coordinate = kind === "Coordinates";
                            const reciprocal = coordinate ? 1 / (x * matrix[3] + y * matrix[7] + z * matrix[11] + matrix[15]) : 1;
                            for (let component = 0; component < 3; ++component) {
                                expected[stride + component] = (x * matrix[component] + y * matrix[4 + component] +
                                    z * matrix[8 + component] + (coordinate ? matrix[12 + component] : 0)) * reciprocal;
                            }
                            const name = kind === "Coordinates" ? "_TransformVector3Coordinates" :
                                kind === "Normals" ? "_TransformVector3Normals" : "_TransformVector4Normals";
                            _native[name](data, { _m: matrix }, stride, stride);
                            for (let index = 0; index < data.length; ++index) {
                                if (data[index] !== expected[index]) {
                                    throw new Error(name + " changed element " + index + ": " + data[index] + " != " + expected[index]);
                                }
                            }
                            for (const range of [[], [undefined], [0, undefined], [0, stride]]) {
                                const entire = original.slice();
                                _native[name](entire, { _m: matrix }, ...range);
                                for (let index = 0; index < entire.length; ++index) {
                                    if (entire[index] !== expected[stride + index]) {
                                        throw new Error(name + " did not honor the default range");
                                    }
                                }
                            }
                        }
                    }
                    for (const Type of [Uint16Array, Uint32Array, Int32Array]) {
                        const indices = new Type([0, 1, 2, 3, 4, 5]);
                        _native._FlipFaces(indices);
                        if (Array.from(indices).join(",") !== "0,2,1,3,5,4") {
                            throw new Error("Face flipping did not honor the default range");
                        }
                    }
                    for (const range of [[-1, 3], [0, -1], [0, 2], [Infinity, 3], [1, 3], [0, 6]]) {
                        let rejected = false;
                        try {
                            _native._TransformVector3Coordinates(new Float32Array(3), { _m: values }, ...range);
                        } catch (error) {
                            rejected = /[Rr]ange/.test(error.message);
                        }
                        if (!rejected) throw new Error("Invalid vertex range was not rejected");
                    }
                    for (const matrix of [null, {}, new Uint8Array(16), new Float32Array(15), new Array(16)]) {
                        let rejected = false;
                        try {
                            _native._TransformVector3Coordinates(new Float32Array(3), { _m: matrix }, 0, 3);
                        } catch (error) {
                            rejected = /[Mm]atrix/.test(error.message);
                        }
                        if (!rejected) throw new Error("Invalid matrix was not rejected");
                    }
                })();
            )", "native-vertex-matrix-storage.js");
            completed.set_value();
        }
        catch (const Napi::Error& error)
        {
            completed.set_exception(std::make_exception_ptr(std::runtime_error{Napi::GetErrorString(error)}));
        }
        catch (...)
        {
            completed.set_exception(std::current_exception());
        }
    });
    if (completion.wait_for(std::chrono::seconds{30}) != std::future_status::ready)
    {
        ADD_FAILURE() << "Timed out waiting for native vertex transforms";
        std::quick_exit(1);
    }
    EXPECT_NO_THROW(completion.get());
#endif
}

TEST(NativeOptimizations, SplatSortingAcceptsTypedAndNumberArrayMatrices)
{
#ifndef HAS_NATIVE_OPTIMIZATIONS
    GTEST_SKIP() << "NativeOptimizations is disabled";
#else
    std::promise<void> completed;
    auto completion = completed.get_future();
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&](Napi::Env env) {
        try
        {
            Babylon::Plugins::NativeOptimizations::Initialize(env);
            Napi::Eval(env, R"(
                (function() {
                    const positions = new Float32Array([
                         1, 0,  0, 1,
                         0, 1,  0, 1,
                         0, 0,  1, 1,
                         1, 1,  1, 1,
                        -1, 0, -1, 1
                    ]);
                    for (const typed of [true, false]) {
                        const matrix = typed ? new Float32Array(16) : new Array(16).fill(0);
                        matrix[2] = 2;
                        matrix[6] = -3;
                        matrix[10] = 5;
                        const modelView = { _m: matrix };
                        for (const rightHanded of [false, true]) {
                            const indices = new Float32Array(5);
                            _native.sortSplats(modelView, positions, indices, rightHanded);
                            // Distinct depths [2, -3, 5, 4, -7] give an algorithm-independent ordering.
                            const expected = rightHanded ? [4, 1, 0, 3, 2] : [2, 3, 0, 1, 4];
                            for (let index = 0; index < indices.length; ++index) {
                                if (indices[index] !== expected[index]) {
                                    throw new Error("Unexpected splat order for typed=" + typed +
                                        ", rightHanded=" + rightHanded + ", index=" + index);
                                }
                            }
                            const single = new Float32Array([99]);
                            _native.sortSplats(modelView, positions.subarray(0, 4), single, rightHanded);
                            if (single[0] !== 0) {
                                throw new Error("Unexpected single-splat index");
                            }
                            _native.sortSplats(modelView, new Float32Array(0), new Float32Array(0), rightHanded);
                        }
                    }
                    function expectMatrixError(matrix, message) {
                        let error;
                        try {
                            _native.sortSplats({ _m: matrix }, positions, new Float32Array(5), false);
                        } catch (caught) {
                            error = caught;
                        }
                        // JSI wraps exceptions thrown from host functions with this prefix.
                        if (!(error instanceof Error) ||
                            (error.message !== message && error.message !== "Exception in HostFunction: " + message)) {
                            throw new Error("Invalid matrix must report: " + message +
                                "; received: " + String(error));
                        }
                    }
                    for (const matrix of [undefined, null, {}, 42, new Uint8Array(16), new Float64Array(16)]) {
                        expectMatrixError(matrix, "sortSplats requires modelView._m to be a Float32Array or Array.");
                    }
                    for (const component of [2, 6, 10]) {
                        const matrix = new Array(16).fill(0);
                        delete matrix[component];
                        expectMatrixError(matrix, "sortSplats requires modelView._m[2], [6], [10] to be numbers.");
                        for (const value of [undefined, null, "2", false, {}]) {
                            matrix[component] = value;
                            expectMatrixError(matrix, "sortSplats requires modelView._m[2], [6], [10] to be numbers.");
                        }
                    }
                })();
            )", "native-splat-matrix-storage.js");
            completed.set_value();
        }
        catch (const Napi::Error& error)
        {
            completed.set_exception(std::make_exception_ptr(std::runtime_error{Napi::GetErrorString(error)}));
        }
        catch (...)
        {
            completed.set_exception(std::current_exception());
        }
    });
    if (completion.wait_for(std::chrono::seconds{30}) != std::future_status::ready)
    {
        // AppRuntime teardown joins the worker; returning would hang if that worker is stuck.
        ADD_FAILURE() << "Timed out waiting for NativeOptimizations matrix storage regression";
        std::quick_exit(1);
    }
    EXPECT_NO_THROW(completion.get());
#endif
}
