if(BABYLON_NATIVE_POLYFILL_WINDOW)
    add_subdirectory(Window)
endif()

if(BABYLON_NATIVE_POLYFILL_CANVAS)
    add_subdirectory(Canvas)
endif()

if(BABYLON_NATIVE_POLYFILL_RENDERTARGETTEXTURE)
    add_subdirectory(RenderTargetTexture)
endif()
