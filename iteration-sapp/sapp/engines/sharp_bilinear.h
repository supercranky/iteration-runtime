#ifndef ITERATION_SHARP_BILINEAR_H
#define ITERATION_SHARP_BILINEAR_H

// Compatible with Sokol GL's public vertex layout and two-matrix uniform block.
// Magnification keeps texel interiors sharp, blending only over one screen pixel
// at their boundaries. Minification retains ordinary bilinear sampling.
static sg_shader make_sharp_bilinear_shader(void)
{
#if defined(SOKOL_GLES3) || defined(SOKOL_GLCORE)
#if defined(SOKOL_GLES3)
#define SHARP_GLSL_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define SHARP_GLSL_VERSION "#version 410\n"
#endif
  sg_shader_desc desc = {0};
  desc.vertex_func.source = SHARP_GLSL_VERSION
    "uniform vec4 vs_params[8];\n"
    "layout(location=0) in vec4 position;\n"
    "layout(location=1) in vec2 texcoord0;\n"
    "layout(location=2) in vec4 color0;\n"
    "layout(location=3) in float psize;\n"
    "out vec4 uv; out vec4 color;\n"
    "void main(){\n"
    "gl_Position=mat4(vs_params[0],vs_params[1],vs_params[2],vs_params[3])*position;\n"
    "gl_PointSize=psize;\n"
    "uv=mat4(vs_params[4],vs_params[5],vs_params[6],vs_params[7])*vec4(texcoord0,0.0,1.0);\n"
    "color=color0; }\n";
  desc.fragment_func.source = SHARP_GLSL_VERSION
    "uniform sampler2D tex_smp;\n"
    "in vec4 uv; in vec4 color; layout(location=0) out vec4 frag_color;\n"
    "void main(){\n"
    "vec2 size=vec2(textureSize(tex_smp,0));\n"
    "vec2 texel=uv.xy*size;\n"
    "vec2 footprint=clamp(fwidth(texel),vec2(0.0001),vec2(1.0));\n"
    "vec2 sharp=floor(texel)+0.5+clamp((fract(texel)-0.5)/footprint,vec2(-0.5),vec2(0.5));\n"
    "frag_color=texture(tex_smp,sharp/size)*color; }\n";
  const char *names[4]={"position","texcoord0","color0","psize"};
  for(int i=0;i<4;i++){
    desc.attrs[i].glsl_name=names[i];
    desc.attrs[i].base_type=SG_SHADERATTRBASETYPE_FLOAT;
  }
  desc.uniform_blocks[0].stage=SG_SHADERSTAGE_VERTEX;
  desc.uniform_blocks[0].size=32*sizeof(float);
  desc.uniform_blocks[0].glsl_uniforms[0].glsl_name="vs_params";
  desc.uniform_blocks[0].glsl_uniforms[0].type=SG_UNIFORMTYPE_FLOAT4;
  desc.uniform_blocks[0].glsl_uniforms[0].array_count=8;
  desc.views[0].texture.stage=SG_SHADERSTAGE_FRAGMENT;
  desc.views[0].texture.image_type=SG_IMAGETYPE_2D;
  desc.views[0].texture.sample_type=SG_IMAGESAMPLETYPE_FLOAT;
  desc.samplers[0].stage=SG_SHADERSTAGE_FRAGMENT;
  desc.samplers[0].sampler_type=SG_SAMPLERTYPE_FILTERING;
  desc.texture_sampler_pairs[0].stage=SG_SHADERSTAGE_FRAGMENT;
  desc.texture_sampler_pairs[0].view_slot=0;
  desc.texture_sampler_pairs[0].sampler_slot=0;
  desc.texture_sampler_pairs[0].glsl_name="tex_smp";
  desc.label="iteration-sharp-bilinear";
#undef SHARP_GLSL_VERSION
  return sg_make_shader(&desc);
#else
  // Other backends retain Sokol GL's default shader.
  return (sg_shader){0};
#endif
}
#endif
