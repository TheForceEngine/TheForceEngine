uniform vec4 ScaleOffset;
in vec2 vtx_pos;
in vec2 vtx_uv;

out vec2 Frag_UV;

void main()
{
    Frag_UV = vec2(vtx_uv.x, 1.0 - vtx_uv.y);
    gl_Position = vec4(vtx_pos.xy * ScaleOffset.xy + ScaleOffset.zw, 0, 1);
}
