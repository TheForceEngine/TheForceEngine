in vec2 Frag_UV;
in vec2 Frag_Pos;
in vec4 Frag_Color;
out vec4 Out_Color;

uniform sampler2D Image;

void main()
{
	vec4 color = texture(Image, Frag_UV) * Frag_Color;

	// For now always apply the filter.
	// TODO: Set as a color ramp to later support sepia tone, etc.
	float lum = color.r * 0.25 + color.g * 0.5 + color.b * 0.25;
	lum = floor(lum * 31.0);
	lum = floor(8.0 + lum*50.0/31.0) / 63.0;
	color.rgb = vec3(lum);

    Out_Color.rgb = color.rgb * color.a;
    Out_Color.a = color.a;
}
