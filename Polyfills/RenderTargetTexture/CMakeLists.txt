set(SOURCES
    "Include/Babylon/Polyfills/RenderTargetTexture.h"
    "Source/RenderTargetTexture.cpp")

add_library(RenderTargetTexture ${SOURCES})
warnings_as_errors(RenderTargetTexture)

target_include_directories(RenderTargetTexture
    PUBLIC "Include")

target_link_libraries(RenderTargetTexture
    PUBLIC napi
    PUBLIC Foundation)

set_property(TARGET RenderTargetTexture PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
