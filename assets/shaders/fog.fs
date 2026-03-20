#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float time;
uniform float opacity;

/* Simple hash for procedural noise */
float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

/* Value noise with smooth interpolation */
float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

/* FBM (fractal Brownian motion) — 3 octaves */
float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 3; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void main()
{
    vec2 uv = fragTexCoord;

    /* Animated fog pattern */
    float t = time * 0.3;
    float f = fbm(uv * 4.0 + vec2(t, t * 0.7));
    f += fbm(uv * 6.0 - vec2(t * 0.5, t * 0.3)) * 0.5;
    f = clamp(f * 0.8, 0.0, 1.0);

    /* Fog color: light gray-blue */
    vec3 fog_color = vec3(0.7, 0.75, 0.85);

    /* Edge fade — softer at card borders */
    float edge = smoothstep(0.0, 0.08, uv.x) * smoothstep(1.0, 0.92, uv.x)
               * smoothstep(0.0, 0.08, uv.y) * smoothstep(1.0, 0.92, uv.y);

    float alpha = f * opacity * edge;
    finalColor = vec4(fog_color, alpha);
}
