#include <bgfx_shader.sh>

#if BGFX_SHADER_LANGUAGE_SPIRV
// shaderc's uniform-block extraction does not recognize Texture2DMSArray.
Texture2DMSArray<vec4> bnDepthResolveSourceTexture : REGISTER(t, 0);
static BgfxSampler2DMSArray bnDepthResolveSource = { bnDepthResolveSourceTexture };
#else
SAMPLER2DMSARRAY(bnDepthResolveSource, 0);
#endif
uniform vec4 bnDepthResolveLayer;

void main()
{
    gl_FragDepth = texelFetch(bnDepthResolveSource, ivec3(gl_FragCoord.xy, bnDepthResolveLayer.x), 0).r;
}
