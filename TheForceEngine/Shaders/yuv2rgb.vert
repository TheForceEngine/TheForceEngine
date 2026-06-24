uniform vec4 ScaleOffset;
// pic_height / frame_height: scales V so we only sample the valid
// picture rows and never touch the macroblock-alignment padding at
// the bottom of the texture (e.g. 1080/1088 for a 1920x1088 OGV).
uniform float UVScale;
in vec2 vtx_pos;
in vec2 vtx_uv;

out vec2 Frag_UV;

void main()
{
    Frag_UV = vec2(vtx_uv.x, (1.0 - vtx_uv.y) * UVScale);
    gl_Position = vec4(vtx_pos.xy * ScaleOffset.xy + ScaleOffset.zw, 0, 1);
}
