#include <bgfx_shader.sh>

SAMPLER2DMS(bnDepthResolveSource, 0);

void main()
{
    gl_FragDepth = texelFetch(bnDepthResolveSource, ivec2(gl_FragCoord.xy), 0).r;
}
