// Shared full screen quad vertex shader for both 2D-Scaler passes.
// Same ScaleOffset/UV convention as blit.vert.
uniform vec4 ScaleOffset;
in vec2 vtx_pos;
in vec2 vtx_uv;

out vec2 Frag_UV;

void main()
{
    Frag_UV = vtx_uv;
    gl_Position = vec4(vtx_pos.xy * ScaleOffset.xy + ScaleOffset.zw, 0, 1);
}
