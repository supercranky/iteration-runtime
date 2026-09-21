@ctype mat4 hmm_mat4

@vs model_vs
layout(binding=0) uniform model_vs_params {
    mat4 mvp;
    mat4 model;
};
in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec4 color0;
out vec2 uv;
out vec3 world_normal;
out vec3 prop_position;
out vec4 vertex_color;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    world_normal = normalize(transpose(inverse(mat3(model))) * normal);
    prop_position = (model * vec4(position, 1.0)).xyz;
    uv = texcoord0;
    vertex_color = color0;
}
@end

@block model_surface
layout(binding=0) uniform texture2D base_color_tex;
layout(binding=0) uniform sampler base_color_smp;
layout(binding=1) uniform model_fs_params {
    vec4 base_color_factor;
    vec4 light_dir_steps;
    vec4 ambient_alpha;
    vec4 uv_transform;
    vec4 point_position_radius[4];
    vec4 point_color_inner[4];
    vec4 point_params; // count; -1 uses the fixed preview light
};
in vec2 uv;
in vec3 world_normal;
in vec3 prop_position;
in vec4 vertex_color;
vec4 surface() {
    float c = cos(ambient_alpha.w);
    float s = sin(ambient_alpha.w);
    vec2 scaled_uv = uv * uv_transform.zw;
    vec2 transformed_uv = vec2(c * scaled_uv.x - s * scaled_uv.y, s * scaled_uv.x + c * scaled_uv.y) + uv_transform.xy;
    vec4 base = texture(sampler2D(base_color_tex, base_color_smp), transformed_uv) * base_color_factor * vertex_color;
    if (ambient_alpha.y > 0.5 && base.a < ambient_alpha.z) discard;
    float diffuse = max(dot(normalize(world_normal), normalize(light_dir_steps.xyz)), 0.0);
    float steps = max(light_dir_steps.w, 1.0);
    diffuse = floor(diffuse * steps + 0.5) / steps;
    vec3 direct = vec3(diffuse);
    if (point_params.x >= 0.0) {
        direct = vec3(0.0);
        for (int i = 0; i < 4; i++) {
            if (float(i) >= point_params.x) break;
            vec3 delta = point_position_radius[i].xyz - prop_position;
            float distance = length(delta);
            float inner = point_color_inner[i].w;
            float outer = point_position_radius[i].w;
            float attenuation = 1.0 - smoothstep(inner, max(outer, inner + 0.001), distance);
            float lambert = max(dot(normalize(world_normal), delta / max(distance, 0.001)), 0.0);
            lambert = floor(lambert * steps + 0.5) / steps;
            // Brightest contribution, like the 2D lighting composition: nearby
            // lamps must not wash out the existing quantized shading.
            direct = max(direct, point_color_inner[i].rgb * lambert * attenuation);
        }
    }
    base.rgb *= ambient_alpha.x + (1.0 - ambient_alpha.x) * clamp(direct, 0.0, 1.0);
    return base;
}
@end

@fs model_fs
@include_block model_surface
out vec4 frag_color;
void main() { frag_color = surface(); }
@end

@fs pixel_fs
@include_block model_surface
layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_geometry;
void main() {
    frag_color = surface();
    // Binary coverage: never antialias the rasterized prop or its outline.
    if (frag_color.a < 0.5) discard;
    frag_color.a = 1.0;
    frag_geometry = vec4(normalize(world_normal) * 0.5 + 0.5, gl_FragCoord.z);
}
@end

@program model model_vs model_fs
@program pixel_model model_vs pixel_fs

@vs pixel_composite_vs
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
@end

@fs pixel_composite_fs
layout(binding=0) uniform texture2D model_color;
layout(binding=0) uniform sampler pixel_sampler;
layout(binding=2) uniform pixel_style_params {
    vec4 palette[64];
    // palette count, silhouette outline enabled, reserved, reserved
    vec4 style;
    // supersample factor (1 or 2), backend render-target Y flip, unused, unused
    vec4 options;
};
in vec2 uv;
out vec4 frag_color;
vec4 color_at(ivec2 p) {
    int factor = int(options.x);
    ivec2 size = textureSize(sampler2D(model_color, pixel_sampler), 0) / factor;
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) return vec4(0.0);
    ivec2 q = p * factor;
    vec4 result = texelFetch(sampler2D(model_color, pixel_sampler), q, 0);
    if (factor == 2) {
        result += texelFetch(sampler2D(model_color, pixel_sampler), q + ivec2(1,0), 0);
        result += texelFetch(sampler2D(model_color, pixel_sampler), q + ivec2(0,1), 0);
        result += texelFetch(sampler2D(model_color, pixel_sampler), q + ivec2(1,1), 0);
        result *= 0.25;
    }
    // Average covered samples only: transparent samples must not darken the
    // silhouette. Coverage is binarized before outline detection, not blended.
    if (result.a > 0.0) result.rgb /= result.a;
    result.a = result.a >= 0.5 ? 1.0 : 0.0;
    return result;
}
void main() {
    vec2 sample_uv = vec2(uv.x, options.y > 0.5 ? 1.0 - uv.y : uv.y);
    ivec2 p = ivec2(floor(sample_uv * vec2(textureSize(sampler2D(model_color, pixel_sampler), 0)) / options.x));
    vec4 base = color_at(p);
    ivec2 dirs[4] = ivec2[4](ivec2(-1,0), ivec2(1,0), ivec2(0,-1), ivec2(0,1));
    bool silhouette = false;
    for (int i = 0; i < 4; i++) {
        ivec2 q = p + dirs[i];
        vec4 neighbor = color_at(q);
        if (base.a < 0.5 && neighbor.a > 0.5) silhouette = true;
    }
    // Only transparent texels neighboring coverage get an outline. Never
    // overwrite an opaque texel at a normal, depth, material or lid seam.
    if (style.y > 0.5 && silhouette) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (base.a < 0.5) discard;
    vec3 chosen = base.rgb;
    float best = 1e10;
    for (int i = 0; i < 64; i++) {
        if (float(i) >= style.x) break;
        vec3 delta = base.rgb - palette[i].rgb;
        float distance = dot(delta * delta, vec3(0.299, 0.587, 0.114));
        if (distance < best) { best = distance; chosen = palette[i].rgb; }
    }
    frag_color = vec4(chosen, 1.0);
}
@end
@program pixel_composite pixel_composite_vs pixel_composite_fs
