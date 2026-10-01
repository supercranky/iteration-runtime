#ifndef ITERATION_SPRITE_TINT_H
#define ITERATION_SPRITE_TINT_H

// Custom sokol-gl pipeline using its public vertex layout and matrix block.
// Position.z carries the blend amount; it never affects geometry/depth.
static sg_shader sprite_tint_shader;
static sgl_pipeline sprite_tint_pipeline;

static int sprite_tint_prepare(void) {
  if (sprite_tint_pipeline.id) return 1;
#if defined(SOKOL_GLES3) || defined(SOKOL_GLCORE)
#if defined(SOKOL_GLES3)
#define SPRITE_TINT_GLSL "#version 300 es\nprecision highp float;\n"
#else
#define SPRITE_TINT_GLSL "#version 410\n"
#endif
  sg_shader_desc shader = {0};
  shader.vertex_func.source = SPRITE_TINT_GLSL
    "uniform vec4 vs_params[8];\n"
    "in vec3 position; in vec2 texcoord0; in vec4 color0;\n"
    "out vec2 uv; out vec4 tint; out float amount;\n"
    "void main(){\n"
    "mat4 mvp=mat4(vs_params[0],vs_params[1],vs_params[2],vs_params[3]);\n"
    "mat4 tm=mat4(vs_params[4],vs_params[5],vs_params[6],vs_params[7]);\n"
    "gl_Position=mvp*vec4(position.xy,0.0,1.0);\n"
    "uv=(tm*vec4(texcoord0,0.0,1.0)).xy; tint=color0; amount=position.z; }\n";
  shader.fragment_func.source = SPRITE_TINT_GLSL
    "uniform sampler2D tex_smp;\n"
    "in vec2 uv; in vec4 tint; in float amount; out vec4 frag_color;\n"
    "void main(){ vec4 texel=texture(tex_smp,uv);\n"
    // Textures and blending are premultiplied. Preserve source coverage.
    "frag_color=vec4(mix(texel.rgb,tint.rgb*texel.a,amount),texel.a)*tint.a; }\n";
#undef SPRITE_TINT_GLSL
  shader.attrs[0].glsl_name = "position";
  shader.attrs[1].glsl_name = "texcoord0";
  shader.attrs[2].glsl_name = "color0";
  for (int i=0;i<3;i++) shader.attrs[i].base_type = SG_SHADERATTRBASETYPE_FLOAT;
  shader.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
  shader.uniform_blocks[0].size = 128;
  shader.uniform_blocks[0].glsl_uniforms[0].glsl_name = "vs_params";
  shader.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
  shader.uniform_blocks[0].glsl_uniforms[0].array_count = 8;
  shader.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
  shader.views[0].texture.image_type = SG_IMAGETYPE_2D;
  shader.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
  shader.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
  shader.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
  shader.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
  shader.texture_sampler_pairs[0].view_slot = 0;
  shader.texture_sampler_pairs[0].sampler_slot = 0;
  shader.texture_sampler_pairs[0].glsl_name = "tex_smp";
  shader.label = "sprite-tint";
  sprite_tint_shader = sg_make_shader(&shader);
  if (sg_query_shader_state(sprite_tint_shader) != SG_RESOURCESTATE_VALID) {
    sg_destroy_shader(sprite_tint_shader);
    sprite_tint_shader.id = 0;
    return 0;
  }
  sprite_tint_pipeline = sgl_make_pipeline(&(sg_pipeline_desc){
    .shader = sprite_tint_shader,
    .sample_count = 4,
    .colors[0].pixel_format = SG_PIXELFORMAT_BGRA8,
    .colors[0].blend = { .enabled = true,
      .src_factor_rgb = SG_BLENDFACTOR_ONE,
      .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .src_factor_alpha = SG_BLENDFACTOR_ONE,
      .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA }
  });
  if (!sprite_tint_pipeline.id) {
    sg_destroy_shader(sprite_tint_shader);
    sprite_tint_shader.id = 0;
    return 0;
  }
  return 1;
#else
  return 0; // Do not silently render the wrong result on unsupported backends.
#endif
}

static void sprite_tint_shutdown(void) {
  if (sprite_tint_pipeline.id) sgl_destroy_pipeline(sprite_tint_pipeline);
  if (sprite_tint_shader.id) sg_destroy_shader(sprite_tint_shader);
  sprite_tint_pipeline.id = 0;
  sprite_tint_shader.id = 0;
}
#endif
