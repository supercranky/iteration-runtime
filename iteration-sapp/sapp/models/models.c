#include "models.h"
#include "../HandmadeMath.h"
#include "sokol_app.h"
#include "sokol_fetch.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_gl.h"
#include "stb/stb_image.h"
#include "util/fileutil.h"
#include "../hot_reload.h"
#include "../asset_fetch.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "../../libs/cgltf/cgltf.h"
#include "models/model.glsl.h"

#define MODEL_ASSETS 32
#define MODEL_INSTANCES 128
#define MODEL_PATH 256
#define MODEL_FILE_CAPACITY (4 * 1024 * 1024)
#define MODEL_NAME 64

typedef struct { float p[3], n[3], uv[2], color[4]; } ModelVertex;
typedef struct {
    sg_buffer vertices, indices;
    uint32_t index_count, material;
    float min[3], max[3];
} ModelPrimitive;
typedef enum { MODEL_ALPHA_OPAQUE, MODEL_ALPHA_MASK, MODEL_ALPHA_BLEND } ModelAlpha;
typedef struct {
    sg_image image;
    sg_view view;
    float factor[4], cutoff, uv_offset[2], uv_scale[2], uv_rotation;
    ModelAlpha alpha;
    int double_sided;
} ModelMaterial;
typedef struct {
    int parent;
    float base_t[3], base_r[4], base_s[3];
    hmm_mat4 base_matrix;
    int uses_matrix;
    uint32_t primitive_start, primitive_count;
} ModelNode;
typedef enum { MODEL_TRANSLATION, MODEL_ROTATION, MODEL_SCALE } ModelPath;
typedef enum { MODEL_LINEAR, MODEL_STEP, MODEL_CUBIC } ModelInterpolation;
typedef struct {
    int node;
    ModelPath path;
    ModelInterpolation interpolation;
    uint32_t count, components;
    float *times, *values;
} ModelChannel;
typedef struct {
    char name[MODEL_NAME];
    float duration;
    ModelChannel *channels;
    uint32_t channel_count;
} ModelAnimation;
typedef struct {
    int used, loading, refs;
    char path[MODEL_PATH];
    ModelPrimitive *primitives;
    uint32_t primitive_count;
    ModelMaterial *materials;
    uint32_t material_count;
    ModelNode *nodes;
    uint32_t node_count;
    ModelAnimation *animations;
    uint32_t animation_count;
    float min[3], max[3];
    JSValue load_callbacks[8];
    int callback_count;
} ModelAsset;
typedef struct {
    int used, asset, animation, playing, loop;
    float time, speed;
    float *t, *r, *s;
    hmm_mat4 *world;
    JSValue completion;
} ModelInstance;
typedef struct { int asset; uint8_t *bytes; } ModelRequest;

#define MODEL_PIXEL_TARGET 128

typedef struct {
    int id;
    double x, y, height, scale, rotation, pixel_size;
    int sprite_layer; // Complete sprite batch preceding this model in Y order.
    int light_count; // -1: fixed preview light; 0..4: scene point lights
    float lights[4][9]; // relative x,height,z, inner/outer radius, RGB, intensity
} ModelDraw;
static ModelDraw draw_queue[MODEL_INSTANCES];
static int draw_count;
static int world_sprite_last_layer;

static struct {
    JSContext *ctx;
    ModelAsset assets[MODEL_ASSETS];
    ModelInstance instances[MODEL_INSTANCES];
    sg_shader shader, pixel_shader, composite_shader;
    sg_pipeline pipelines[2][2][3]; /* double-sided, pixel mode, alpha */
    sg_pipeline composite_pipeline;
    sg_image pixel_color, pixel_geometry, pixel_depth;
    sg_view pixel_color_view, pixel_geometry_view;
    sg_attachments pixel_attachments;
    sg_sampler pixel_sampler;
    pixel_style_params_t pixel_style;
    int pixel_enabled;
    sg_sampler sampler;
    sg_image white_image;
    sg_view white_view;
    float pitch, yaw, ambient, steps;
    float light_dir[3];
} models;

static hmm_mat4 mat_mul(hmm_mat4 a, hmm_mat4 b) { return HMM_MultiplyMat4(a, b); }
static hmm_quaternion quat(float *q) { hmm_quaternion r = { .X=q[0], .Y=q[1], .Z=q[2], .W=q[3] }; return r; }
static hmm_mat4 trs(float *t, float *r, float *s) {
    return mat_mul(HMM_Translate(HMM_Vec3(t[0],t[1],t[2])),
        mat_mul(HMM_QuaternionToMat4(quat(r)), HMM_Scale(HMM_Vec3(s[0],s[1],s[2]))));
}
static void safe_name(char dst[MODEL_NAME], const char *src, const char *fallback) {
    snprintf(dst, MODEL_NAME, "%s", src && src[0] ? src : fallback);
}
static int asset_index(ModelAsset *a) { return (int)(a - models.assets); }

static void free_asset(ModelAsset *a) {
    if (!a->used) return;
    for (uint32_t i=0;i<a->primitive_count;i++) {
        sg_destroy_buffer(a->primitives[i].vertices); sg_destroy_buffer(a->primitives[i].indices);
    }
    for (uint32_t i=0;i<a->material_count;i++) if (a->materials[i].image.id) {
        sg_destroy_view(a->materials[i].view); sg_destroy_image(a->materials[i].image);
    }
    for (uint32_t i=0;i<a->animation_count;i++) {
        for (uint32_t j=0;j<a->animations[i].channel_count;j++) {
            free(a->animations[i].channels[j].times); free(a->animations[i].channels[j].values);
        }
        free(a->animations[i].channels);
    }
    for (int i=0;i<a->callback_count;i++) JS_FreeValue(models.ctx,a->load_callbacks[i]);
    free(a->primitives); free(a->materials); free(a->nodes); free(a->animations);
    memset(a,0,sizeof(*a));
}
static void notify_load(ModelAsset *a, const char *error) {
    JSValue info = JS_NewObject(models.ctx);
    JS_SetPropertyStr(models.ctx,info,"id",JS_NewInt32(models.ctx,asset_index(a)+1));
    JS_SetPropertyStr(models.ctx,info,"src",JS_NewString(models.ctx,a->path));
    JS_SetPropertyStr(models.ctx,info,"error",error?JS_NewString(models.ctx,error):JS_NULL);
    JSValue names=JS_NewArray(models.ctx);
    for(uint32_t i=0;i<a->animation_count;i++) JS_SetPropertyUint32(models.ctx,names,i,JS_NewString(models.ctx,a->animations[i].name));
    JS_SetPropertyStr(models.ctx,info,"animations",names);
    for(int i=0;i<a->callback_count;i++) {
        JSValue arg=JS_DupValue(models.ctx,info);
        JSValue ret=JS_Call(models.ctx,a->load_callbacks[i],JS_UNDEFINED,1,(JSValueConst*)&arg);
        JS_FreeValue(models.ctx,arg); JS_FreeValue(models.ctx,ret); JS_FreeValue(models.ctx,a->load_callbacks[i]);
    }
    a->callback_count=0; JS_FreeValue(models.ctx,info);
}
static cgltf_accessor *attribute(cgltf_primitive *p,cgltf_attribute_type type,int index) {
    for(cgltf_size i=0;i<p->attributes_count;i++) if(p->attributes[i].type==type && p->attributes[i].index==index) return p->attributes[i].data;
    return NULL;
}
static int texture_from_view(ModelMaterial *out, cgltf_texture_view *view) {
    if(!view || !view->texture || !view->texture->image || !view->texture->image->buffer_view) return 1;
    cgltf_buffer_view *bv=view->texture->image->buffer_view;
    const uint8_t *bytes=(const uint8_t*)bv->buffer->data+bv->offset;
    int w,h,c; unsigned char *rgba=stbi_load_from_memory(bytes,(int)bv->size,&w,&h,&c,4);
    if(!rgba)return 0;
    out->image=sg_make_image(&(sg_image_desc){.width=w,.height=h,.pixel_format=SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0]={.ptr=rgba,.size=(size_t)w*h*4},.label="model texture"});
    out->view=sg_make_view(&(sg_view_desc){.texture.image=out->image,.label="model texture view"});
    stbi_image_free(rgba); return out->image.id!=0;
}
static int build_asset(ModelAsset *a,const void *bytes,size_t size,const char **error) {
    cgltf_options options={0}; cgltf_data *data=NULL;
    if(cgltf_parse(&options,bytes,size,&data)!=cgltf_result_success){*error="invalid GLB";return 0;}
    if(data->file_type!=cgltf_file_type_glb){*error="only GLB is supported";goto fail;}
    if(cgltf_load_buffers(&options,data,NULL)!=cgltf_result_success || cgltf_validate(data)!=cgltf_result_success){*error="invalid GLB buffers";goto fail;}
    for(cgltf_size i=0;i<data->extensions_required_count;i++) if(strcmp(data->extensions_required[i],"KHR_mesh_quantization")) {
        *error="unsupported required glTF extension"; goto fail;
    }
    if(data->skins_count || data->meshes_count==0){*error=data->skins_count?"skins are not supported":"GLB has no meshes";goto fail;}
    a->material_count=(uint32_t)(data->materials_count?data->materials_count:1);
    a->materials=calloc(a->material_count,sizeof(*a->materials)); if(!a->materials){*error="out of memory";goto fail;}
    for(uint32_t i=0;i<a->material_count;i++) {
        ModelMaterial *m=&a->materials[i]; m->factor[0]=m->factor[1]=m->factor[2]=m->factor[3]=1; m->cutoff=.5f; m->uv_scale[0]=m->uv_scale[1]=1;
        if(i<data->materials_count) {
            cgltf_material *gm=&data->materials[i]; m->double_sided=gm->double_sided; m->cutoff=gm->alpha_cutoff;
            m->alpha=gm->alpha_mode==cgltf_alpha_mode_mask?MODEL_ALPHA_MASK:gm->alpha_mode==cgltf_alpha_mode_blend?MODEL_ALPHA_BLEND:MODEL_ALPHA_OPAQUE;
            if(gm->has_pbr_metallic_roughness) {
                cgltf_texture_view *base_view=&gm->pbr_metallic_roughness.base_color_texture;
                memcpy(m->factor,gm->pbr_metallic_roughness.base_color_factor,sizeof(m->factor));
                if(base_view->has_transform){memcpy(m->uv_offset,base_view->transform.offset,sizeof(m->uv_offset));memcpy(m->uv_scale,base_view->transform.scale,sizeof(m->uv_scale));m->uv_rotation=base_view->transform.rotation;}
                if(!texture_from_view(m,base_view)){*error="could not decode model texture";goto fail;}
            }
        }
    }
    for(cgltf_size i=0;i<data->meshes_count;i++) a->primitive_count+=(uint32_t)data->meshes[i].primitives_count;
    a->primitives=calloc(a->primitive_count,sizeof(*a->primitives)); if(!a->primitives){*error="out of memory";goto fail;}
    a->min[0]=a->min[1]=a->min[2]=FLT_MAX;a->max[0]=a->max[1]=a->max[2]=-FLT_MAX;
    uint32_t pi=0;
    for(cgltf_size mi=0;mi<data->meshes_count;mi++) for(cgltf_size pj=0;pj<data->meshes[mi].primitives_count;pj++,pi++) {
        cgltf_primitive *gp=&data->meshes[mi].primitives[pj]; ModelPrimitive *p=&a->primitives[pi];
        cgltf_accessor *pos=attribute(gp,cgltf_attribute_type_position,0),*normal=attribute(gp,cgltf_attribute_type_normal,0),*uv=attribute(gp,cgltf_attribute_type_texcoord,0),*color=attribute(gp,cgltf_attribute_type_color,0);
        if(gp->type!=cgltf_primitive_type_triangles || !pos || !gp->indices){*error="model requires indexed triangle POSITION data";goto fail;}
        ModelVertex *verts=calloc(pos->count,sizeof(*verts)); uint32_t *indices=malloc(gp->indices->count*sizeof(*indices));
        if(!verts||!indices){free(verts);free(indices);*error="out of memory";goto fail;}
        p->min[0]=p->min[1]=p->min[2]=FLT_MAX;p->max[0]=p->max[1]=p->max[2]=-FLT_MAX;
        for(cgltf_size v=0;v<pos->count;v++) {
            cgltf_accessor_read_float(pos,v,verts[v].p,3);
            if(normal)cgltf_accessor_read_float(normal,v,verts[v].n,3);else verts[v].n[1]=1;
            if(uv)cgltf_accessor_read_float(uv,v,verts[v].uv,2);
            if(color)cgltf_accessor_read_float(color,v,verts[v].color,4);else verts[v].color[0]=verts[v].color[1]=verts[v].color[2]=verts[v].color[3]=1;
            for(int k=0;k<3;k++){if(verts[v].p[k]<p->min[k])p->min[k]=verts[v].p[k];if(verts[v].p[k]>p->max[k])p->max[k]=verts[v].p[k];}
        }
        for(cgltf_size j=0;j<gp->indices->count;j++)indices[j]=(uint32_t)cgltf_accessor_read_index(gp->indices,j);
        p->vertices=sg_make_buffer(&(sg_buffer_desc){.data={verts,pos->count*sizeof(*verts)},.label="model vertices"});
        p->indices=sg_make_buffer(&(sg_buffer_desc){.usage.index_buffer=true,.data={indices,gp->indices->count*sizeof(*indices)},.label="model indices"});
        p->index_count=(uint32_t)gp->indices->count; p->material=gp->material?(uint32_t)(gp->material-data->materials):0;
        free(verts);free(indices); if(!p->vertices.id||!p->indices.id){*error="could not create model buffers";goto fail;}
    }
    a->node_count=(uint32_t)data->nodes_count;a->nodes=calloc(a->node_count,sizeof(*a->nodes));if(!a->nodes){*error="out of memory";goto fail;}
    for(uint32_t i=0;i<a->node_count;i++) {
        cgltf_node *gn=&data->nodes[i];ModelNode *n=&a->nodes[i];n->parent=gn->parent?(int)(gn->parent-data->nodes):-1;
        n->base_t[0]=n->base_t[1]=n->base_t[2]=0;n->base_r[0]=n->base_r[1]=n->base_r[2]=0;n->base_r[3]=1;n->base_s[0]=n->base_s[1]=n->base_s[2]=1;
        if(gn->has_translation)memcpy(n->base_t,gn->translation,sizeof(n->base_t));if(gn->has_rotation)memcpy(n->base_r,gn->rotation,sizeof(n->base_r));if(gn->has_scale)memcpy(n->base_s,gn->scale,sizeof(n->base_s));
        if(gn->has_matrix){memcpy(&n->base_matrix,gn->matrix,sizeof(gn->matrix));n->uses_matrix=1;}
        if(gn->mesh){n->primitive_start=0;for(cgltf_mesh *m=data->meshes;m<gn->mesh;m++)n->primitive_start+=(uint32_t)m->primitives_count;n->primitive_count=(uint32_t)gn->mesh->primitives_count;}
    }
    a->animation_count=(uint32_t)data->animations_count;a->animations=calloc(a->animation_count,sizeof(*a->animations));if(a->animation_count&&!a->animations){*error="out of memory";goto fail;}
    for(uint32_t ai=0;ai<a->animation_count;ai++) {
        cgltf_animation *ga=&data->animations[ai];ModelAnimation *an=&a->animations[ai];char fallback[32];snprintf(fallback,sizeof(fallback),"animation%u",ai);safe_name(an->name,ga->name,fallback);
        an->channel_count=(uint32_t)ga->channels_count;an->channels=calloc(an->channel_count,sizeof(*an->channels));if(!an->channels){*error="out of memory";goto fail;}
        for(uint32_t ci=0;ci<an->channel_count;ci++) {
            cgltf_animation_channel *gc=&ga->channels[ci];ModelChannel *ch=&an->channels[ci];cgltf_animation_sampler *sm=gc->sampler;
            if(!gc->target_node||gc->target_path==cgltf_animation_path_type_weights){*error="unsupported animation channel";goto fail;}
            ch->node=(int)(gc->target_node-data->nodes);ch->path=gc->target_path==cgltf_animation_path_type_rotation?MODEL_ROTATION:gc->target_path==cgltf_animation_path_type_scale?MODEL_SCALE:MODEL_TRANSLATION;
            ch->components=ch->path==MODEL_ROTATION?4:3;ch->count=(uint32_t)sm->input->count;ch->interpolation=sm->interpolation==cgltf_interpolation_type_step?MODEL_STEP:sm->interpolation==cgltf_interpolation_type_cubic_spline?MODEL_CUBIC:MODEL_LINEAR;
            uint32_t mul=ch->interpolation==MODEL_CUBIC?3:1;ch->times=malloc(ch->count*sizeof(float));ch->values=malloc(ch->count*ch->components*mul*sizeof(float));if(!ch->times||!ch->values){*error="out of memory";goto fail;}
            for(uint32_t k=0;k<ch->count;k++){cgltf_accessor_read_float(sm->input,k,&ch->times[k],1);if(ch->times[k]>an->duration)an->duration=ch->times[k];}
            for(uint32_t k=0;k<ch->count*mul;k++)cgltf_accessor_read_float(sm->output,k,&ch->values[k*ch->components],ch->components);
        }
    }
    cgltf_free(data);return 1;
fail: cgltf_free(data);return 0;
}
static void fetch_model(const sfetch_response_t *r) {
    ModelRequest *req=(ModelRequest*)r->user_data;ModelAsset *a=&models.assets[req->asset];const char *error=NULL;
    if(!r->fetched||!build_asset(a,r->data.ptr,r->data.size,&error)){a->loading=0;notify_load(a,error?error:"could not load GLB");free_asset(a);}
    else {a->loading=0;notify_load(a,NULL);}
    free(req->bytes);
}

static void reset_pose(ModelInstance *in) {
    ModelAsset *a=&models.assets[in->asset];for(uint32_t i=0;i<a->node_count;i++){memcpy(in->t+i*3,a->nodes[i].base_t,12);memcpy(in->r+i*4,a->nodes[i].base_r,16);memcpy(in->s+i*3,a->nodes[i].base_s,12);}
}
static void eval_channel(ModelChannel *c,float time,ModelInstance *in) {
    if(!c->count)return;uint32_t a=0;while(a+1<c->count&&c->times[a+1]<=time)a++;uint32_t b=a+1<c->count?a+1:a;float u=0;if(b!=a&&c->times[b]>c->times[a])u=(time-c->times[a])/(c->times[b]-c->times[a]);if(c->interpolation==MODEL_STEP)u=0;
    uint32_t stride=c->components*(c->interpolation==MODEL_CUBIC?3:1),off=c->interpolation==MODEL_CUBIC?c->components:0;float *va=&c->values[a*stride+off],*vb=&c->values[b*stride+off],*dst=c->path==MODEL_TRANSLATION?in->t+c->node*3:c->path==MODEL_SCALE?in->s+c->node*3:in->r+c->node*4;
    if(c->path==MODEL_ROTATION){hmm_quaternion q=HMM_NLerp(quat(va),u,quat(vb));dst[0]=q.X;dst[1]=q.Y;dst[2]=q.Z;dst[3]=q.W;}else for(uint32_t i=0;i<c->components;i++)dst[i]=va[i]+(vb[i]-va[i])*u;
}
static hmm_mat4 node_world(ModelInstance *in,int index,uint8_t *done) {
    if(done[index])return in->world[index];ModelAsset *a=&models.assets[in->asset];ModelNode *n=&a->nodes[index];hmm_mat4 local=n->uses_matrix?n->base_matrix:trs(in->t+index*3,in->r+index*4,in->s+index*3);
    in->world[index]=n->parent>=0?mat_mul(node_world(in,n->parent,done),local):local;done[index]=1;return in->world[index];
}
static void evaluate(ModelInstance *in) {
    ModelAsset *a=&models.assets[in->asset];reset_pose(in);if(in->animation>=0){ModelAnimation *an=&a->animations[in->animation];for(uint32_t i=0;i<an->channel_count;i++)eval_channel(&an->channels[i],in->time,in);}
    uint8_t *done=calloc(a->node_count,1);if(done){for(uint32_t i=0;i<a->node_count;i++)node_world(in,(int)i,done);free(done);}
}
void models_update(double dt) {
    for(int i=0;i<MODEL_INSTANCES;i++){ModelInstance *in=&models.instances[i];if(!in->used||!in->playing||in->animation<0)continue;ModelAnimation *an=&models.assets[in->asset].animations[in->animation];in->time+=(float)dt*in->speed;if(an->duration>0&&in->time>=an->duration){if(in->loop)in->time=fmodf(in->time,an->duration);else{in->time=an->duration;in->playing=0;if(JS_IsFunction(models.ctx,in->completion)){JSValue callback=JS_DupValue(models.ctx,in->completion);JS_FreeValue(models.ctx,in->completion);in->completion=JS_UNDEFINED;JSValue name=JS_NewString(models.ctx,an->name);JSValue ret=JS_Call(models.ctx,callback,JS_UNDEFINED,1,(JSValueConst*)&name);JS_FreeValue(models.ctx,name);JS_FreeValue(models.ctx,ret);JS_FreeValue(models.ctx,callback);}}}evaluate(in);}
}

static void resize_pixel_targets(int factor) {
    if(models.pixel_color.id) {
        sg_destroy_view(models.pixel_color_view); sg_destroy_view(models.pixel_geometry_view);
        sg_destroy_view(models.pixel_attachments.colors[0]); sg_destroy_view(models.pixel_attachments.colors[1]); sg_destroy_view(models.pixel_attachments.depth_stencil);
        sg_destroy_image(models.pixel_color); sg_destroy_image(models.pixel_geometry); sg_destroy_image(models.pixel_depth);
    }
    int size=MODEL_PIXEL_TARGET*factor;
    models.pixel_color=sg_make_image(&(sg_image_desc){.usage.color_attachment=true,.width=size,.height=size,.pixel_format=SG_PIXELFORMAT_RGBA8,.sample_count=1,.label="model pixel color"});
    models.pixel_geometry=sg_make_image(&(sg_image_desc){.usage.color_attachment=true,.width=size,.height=size,.pixel_format=SG_PIXELFORMAT_RGBA8,.sample_count=1,.label="model pixel normal-depth"});
    models.pixel_depth=sg_make_image(&(sg_image_desc){.usage.depth_stencil_attachment=true,.width=size,.height=size,.pixel_format=SG_PIXELFORMAT_DEPTH_STENCIL,.sample_count=1,.label="model pixel depth"});
    models.pixel_color_view=sg_make_view(&(sg_view_desc){.texture.image=models.pixel_color});
    models.pixel_geometry_view=sg_make_view(&(sg_view_desc){.texture.image=models.pixel_geometry});
    models.pixel_attachments=(sg_attachments){
        .colors={sg_make_view(&(sg_view_desc){.color_attachment.image=models.pixel_color}),sg_make_view(&(sg_view_desc){.color_attachment.image=models.pixel_geometry})},
        .depth_stencil=sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image=models.pixel_depth})};
}

void models_init(JSContext *ctx) {
    memset(&models,0,sizeof(models));models.ctx=ctx;models.pitch=45;models.ambient=.7f;models.steps=4;models.light_dir[0]=-.4f;models.light_dir[1]=.8f;models.light_dir[2]=.3f;
    models.shader=sg_make_shader(model_shader_desc(sg_query_backend()));models.sampler=sg_make_sampler(&(sg_sampler_desc){.min_filter=SG_FILTER_NEAREST,.mag_filter=SG_FILTER_NEAREST,.wrap_u=SG_WRAP_REPEAT,.wrap_v=SG_WRAP_REPEAT});
    uint32_t white=0xffffffff;models.white_image=sg_make_image(&(sg_image_desc){.width=1,.height=1,.pixel_format=SG_PIXELFORMAT_RGBA8,.data.mip_levels[0]=SG_RANGE(white),.label="model white"});models.white_view=sg_make_view(&(sg_view_desc){.texture.image=models.white_image});
    models.pixel_shader=sg_make_shader(pixel_model_shader_desc(sg_query_backend()));
    models.composite_shader=sg_make_shader(pixel_composite_shader_desc(sg_query_backend()));
    models.pixel_sampler=sg_make_sampler(&(sg_sampler_desc){.min_filter=SG_FILTER_NEAREST,.mag_filter=SG_FILTER_NEAREST,.wrap_u=SG_WRAP_CLAMP_TO_EDGE,.wrap_v=SG_WRAP_CLAMP_TO_EDGE});
    resize_pixel_targets(1);
    models.pixel_style.options[0]=1;
    models.composite_pipeline=sg_make_pipeline(&(sg_pipeline_desc){.shader=models.composite_shader,.sample_count=sapp_sample_count(),
        .depth={.pixel_format=SG_PIXELFORMAT_DEPTH_STENCIL,.compare=SG_COMPAREFUNC_ALWAYS,.write_enabled=false},
        .colors[0].blend={.enabled=true,.src_factor_rgb=SG_BLENDFACTOR_SRC_ALPHA,.dst_factor_rgb=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,.src_factor_alpha=SG_BLENDFACTOR_ONE,.dst_factor_alpha=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA},.label="model nearest palette outline"});
    for(int ds=0;ds<2;ds++)for(int pixel=0;pixel<2;pixel++)for(int alpha=0;alpha<3;alpha++)models.pipelines[ds][pixel][alpha]=sg_make_pipeline(&(sg_pipeline_desc){
        .shader=pixel?models.pixel_shader:models.shader,.layout.attrs={[ATTR_model_position]={.format=SG_VERTEXFORMAT_FLOAT3},[ATTR_model_normal]={.format=SG_VERTEXFORMAT_FLOAT3},[ATTR_model_texcoord0]={.format=SG_VERTEXFORMAT_FLOAT2},[ATTR_model_color0]={.format=SG_VERTEXFORMAT_FLOAT4}},
        .index_type=SG_INDEXTYPE_UINT32,.cull_mode=ds?SG_CULLMODE_NONE:SG_CULLMODE_BACK,.face_winding=SG_FACEWINDING_CCW,.sample_count=pixel?1:sapp_sample_count(),
        .color_count=pixel?2:1,
        .colors[0].pixel_format=pixel?SG_PIXELFORMAT_RGBA8:sg_query_desc().environment.defaults.color_format,
        .colors[1].pixel_format=pixel?SG_PIXELFORMAT_RGBA8:SG_PIXELFORMAT_NONE,
        .depth={.pixel_format=SG_PIXELFORMAT_DEPTH_STENCIL,.compare=SG_COMPAREFUNC_LESS_EQUAL,.write_enabled=alpha!=MODEL_ALPHA_BLEND},
        .colors[0].blend={.enabled=!pixel&&alpha==MODEL_ALPHA_BLEND,.src_factor_rgb=SG_BLENDFACTOR_SRC_ALPHA,.dst_factor_rgb=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,.src_factor_alpha=SG_BLENDFACTOR_ONE,.dst_factor_alpha=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA},.label="model pipeline"});
}
void models_shutdown(void) {
    sg_destroy_pipeline(models.composite_pipeline);
    sg_destroy_shader(models.composite_shader);
    sg_destroy_shader(models.pixel_shader);
    sg_destroy_sampler(models.pixel_sampler);
    sg_destroy_view(models.pixel_color_view); sg_destroy_view(models.pixel_geometry_view);
    sg_destroy_view(models.pixel_attachments.colors[0]); sg_destroy_view(models.pixel_attachments.colors[1]); sg_destroy_view(models.pixel_attachments.depth_stencil);
    sg_destroy_image(models.pixel_color); sg_destroy_image(models.pixel_geometry); sg_destroy_image(models.pixel_depth);
    for(int i=0;i<MODEL_INSTANCES;i++)if(models.instances[i].used){JS_FreeValue(models.ctx,models.instances[i].completion);free(models.instances[i].t);free(models.instances[i].r);free(models.instances[i].s);free(models.instances[i].world);}
    for(int i=0;i<MODEL_ASSETS;i++)free_asset(&models.assets[i]);for(int a=0;a<2;a++)for(int b=0;b<2;b++)for(int c=0;c<3;c++)sg_destroy_pipeline(models.pipelines[a][b][c]);sg_destroy_view(models.white_view);sg_destroy_image(models.white_image);sg_destroy_sampler(models.sampler);sg_destroy_shader(models.shader);memset(&models,0,sizeof(models));
}
JSValue js_models_load(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    (void)this_val;(void)argc;const char *path=JS_ToCString(ctx,argv[0]);if(!path)return JS_EXCEPTION;int slot=-1;
    for(int i=0;i<MODEL_ASSETS;i++)if(models.assets[i].used&&!strcmp(models.assets[i].path,path)){slot=i;break;}if(slot<0)for(int i=0;i<MODEL_ASSETS;i++)if(!models.assets[i].used){slot=i;break;}if(slot<0){JS_FreeCString(ctx,path);return JS_ThrowInternalError(ctx,"model asset limit reached");}
    ModelAsset *a=&models.assets[slot];if(a->used){if(JS_IsFunction(ctx,argv[1])){if(a->loading&&a->callback_count<8)a->load_callbacks[a->callback_count++]=JS_DupValue(ctx,argv[1]);else{JSValue info=JS_NewObject(ctx);JS_SetPropertyStr(ctx,info,"id",JS_NewInt32(ctx,slot+1));JSValue names=JS_NewArray(ctx);for(uint32_t i=0;i<a->animation_count;i++)JS_SetPropertyUint32(ctx,names,i,JS_NewString(ctx,a->animations[i].name));JS_SetPropertyStr(ctx,info,"animations",names);JSValue ret=JS_Call(ctx,argv[1],JS_UNDEFINED,1,(JSValueConst*)&info);JS_FreeValue(ctx,info);JS_FreeValue(ctx,ret);}}JS_FreeCString(ctx,path);return JS_NewInt32(ctx,slot+1);}
    memset(a,0,sizeof(*a));a->used=a->loading=1;snprintf(a->path,sizeof(a->path),"%s",path);if(JS_IsFunction(ctx,argv[1]))a->load_callbacks[a->callback_count++]=JS_DupValue(ctx,argv[1]);ModelRequest *req=calloc(1,sizeof(*req));req->asset=slot;req->bytes=malloc(MODEL_FILE_CAPACITY);char resolved[512];asset_fetch(&(sfetch_request_t){.path=hot_reload_asset_path(path,resolved,sizeof(resolved)),.callback=fetch_model,.buffer={req->bytes,MODEL_FILE_CAPACITY},.user_data={.ptr=req,.size=sizeof(*req)}});free(req);JS_FreeCString(ctx,path);return JS_NewInt32(ctx,slot+1);
}
JSValue js_models_unload(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;int id;if(JS_ToInt32(ctx,&id,argv[0]))return JS_EXCEPTION;if(id>0&&id<=MODEL_ASSETS&&models.assets[id-1].refs==0&&!models.assets[id-1].loading)free_asset(&models.assets[id-1]);return JS_UNDEFINED;}
JSValue js_models_create(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;int id;if(JS_ToInt32(ctx,&id,argv[0]))return JS_EXCEPTION;if(id<1||id>MODEL_ASSETS||!models.assets[id-1].used||models.assets[id-1].loading)return JS_ThrowTypeError(ctx,"model asset is not loaded");int slot=-1;for(int i=0;i<MODEL_INSTANCES;i++)if(!models.instances[i].used){slot=i;break;}if(slot<0)return JS_ThrowInternalError(ctx,"model instance limit reached");ModelAsset *a=&models.assets[id-1];ModelInstance *in=&models.instances[slot];memset(in,0,sizeof(*in));in->used=1;in->asset=id-1;in->animation=-1;in->speed=1;in->completion=JS_UNDEFINED;in->t=malloc(a->node_count*3*sizeof(float));in->r=malloc(a->node_count*4*sizeof(float));in->s=malloc(a->node_count*3*sizeof(float));in->world=malloc(a->node_count*sizeof(hmm_mat4));if(!in->t||!in->r||!in->s||!in->world){in->used=0;free(in->t);free(in->r);free(in->s);free(in->world);return JS_ThrowOutOfMemory(ctx);}a->refs++;evaluate(in);return JS_NewInt32(ctx,slot+1);}
JSValue js_models_destroy(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;int id;if(JS_ToInt32(ctx,&id,argv[0]))return JS_EXCEPTION;if(id>0&&id<=MODEL_INSTANCES){ModelInstance *in=&models.instances[id-1];if(in->used){models.assets[in->asset].refs--;JS_FreeValue(ctx,in->completion);free(in->t);free(in->r);free(in->s);free(in->world);memset(in,0,sizeof(*in));}}return JS_UNDEFINED;}
JSValue js_models_play(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;int id;if(JS_ToInt32(ctx,&id,argv[0])||id<1||id>MODEL_INSTANCES||!models.instances[id-1].used)return JS_ThrowTypeError(ctx,"invalid model instance");const char *name=JS_ToCString(ctx,argv[1]);ModelInstance *in=&models.instances[id-1];ModelAsset *a=&models.assets[in->asset];int found=-1;for(uint32_t i=0;i<a->animation_count;i++)if(!strcmp(a->animations[i].name,name)){found=(int)i;break;}JS_FreeCString(ctx,name);if(found<0)return JS_ThrowRangeError(ctx,"unknown model animation");in->animation=found;in->time=0;in->speed=1;in->loop=0;double speed=1;if(argc>2)JS_ToFloat64(ctx,&speed,argv[2]);in->speed=(float)speed;if(argc>3)in->loop=JS_ToBool(ctx,argv[3]);JS_FreeValue(ctx,in->completion);in->completion=argc>4&&JS_IsFunction(ctx,argv[4])?JS_DupValue(ctx,argv[4]):JS_UNDEFINED;in->playing=1;evaluate(in);return JS_UNDEFINED;}
JSValue js_models_stop(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;int id;if(JS_ToInt32(ctx,&id,argv[0]))return JS_EXCEPTION;if(id>0&&id<=MODEL_INSTANCES)models.instances[id-1].playing=0;return JS_UNDEFINED;}
JSValue js_models_set_time(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;int id;double time;if(JS_ToInt32(ctx,&id,argv[0])||JS_ToFloat64(ctx,&time,argv[1]))return JS_EXCEPTION;if(id>0&&id<=MODEL_INSTANCES&&models.instances[id-1].used){ModelInstance *in=&models.instances[id-1];in->time=(float)time;if(in->animation>=0){float d=models.assets[in->asset].animations[in->animation].duration;if(d>0){if(in->loop)in->time=fmodf(fmaxf(0,in->time),d);else in->time=fminf(fmaxf(0,in->time),d);}}evaluate(in);}return JS_UNDEFINED;}
JSValue js_models_set_pixel_style(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(argc<3)return JS_ThrowTypeError(ctx,"pixel style requires enabled, palette, outline");
    pixel_style_params_t style={0};
    JSValue length=JS_GetPropertyStr(ctx,argv[1],"length");
    uint32_t count=0;
    int error=JS_ToUint32(ctx,&count,length); JS_FreeValue(ctx,length);
    if(error)return JS_EXCEPTION;
    if(count>192 || count%3)return JS_ThrowRangeError(ctx,"palette must contain at most 64 RGB triplets");
    for(uint32_t i=0;i<count;i++) {
        JSValue value=JS_GetPropertyUint32(ctx,argv[1],i); double component;
        error=JS_ToFloat64(ctx,&component,value); JS_FreeValue(ctx,value);
        if(error)return JS_EXCEPTION;
        if(!isfinite(component)||component<0||component>255)return JS_ThrowRangeError(ctx,"palette channels must be 0..255");
        style.palette[i/3][i%3]=(float)(component/255.0);
    }
    int enabled=JS_ToBool(ctx,argv[0]),outline=JS_ToBool(ctx,argv[2]);
    if(enabled<0||outline<0)return JS_EXCEPTION;
    style.style[0]=(float)(count/3);style.style[1]=(float)outline;
    style.options[1]=(sg_query_backend()==SG_BACKEND_GLCORE||sg_query_backend()==SG_BACKEND_GLES3)?0.0f:1.0f;
    int antialias=argc>3?JS_ToBool(ctx,argv[3]):0;
    if(antialias<0)return JS_EXCEPTION;
    style.options[0]=antialias?2.0f:1.0f;
    double dither=0;
    if(argc>4 && !JS_IsUndefined(argv[4]) && JS_ToFloat64(ctx,&dither,argv[4]))return JS_EXCEPTION;
    if(!isfinite(dither)||dither<0||dither>1)return JS_ThrowRangeError(ctx,"dither strength must be 0..1");
    style.style[2]=(float)dither;
    if(models.pixel_style.options[0]!=style.options[0])resize_pixel_targets((int)style.options[0]);
    models.pixel_style=style;models.pixel_enabled=enabled;
    return JS_UNDEFINED;
}

JSValue js_models_set_camera(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;double p,y;if(JS_ToFloat64(ctx,&p,argv[0])||JS_ToFloat64(ctx,&y,argv[1]))return JS_EXCEPTION;models.pitch=(float)p;models.yaw=(float)y;return JS_UNDEFINED;}
JSValue js_models_set_lighting(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;double a,s;if(JS_ToFloat64(ctx,&a,argv[0])||JS_ToFloat64(ctx,&s,argv[1]))return JS_EXCEPTION;models.ambient=(float)a;models.steps=(float)s;return JS_UNDEFINED;}
JSValue js_models_draw(JSContext *ctx,JSValueConst t,int argc,JSValueConst *argv){(void)t;(void)argc;int id;double x,y,height,scale,rotation;if(JS_ToInt32(ctx,&id,argv[0])||JS_ToFloat64(ctx,&x,argv[1])||JS_ToFloat64(ctx,&y,argv[2])||JS_ToFloat64(ctx,&height,argv[3])||JS_ToFloat64(ctx,&scale,argv[4])||JS_ToFloat64(ctx,&rotation,argv[5]))return JS_EXCEPTION;if(id<1||id>MODEL_INSTANCES||!models.instances[id-1].used)return JS_UNDEFINED;
    if(draw_count>=MODEL_INSTANCES)return JS_ThrowRangeError(ctx,"model draw limit reached");
    double pixel_size=1;
    if(argc>6 && !JS_IsUndefined(argv[6]) && JS_ToFloat64(ctx,&pixel_size,argv[6]))return JS_EXCEPTION;
    if(!isfinite(x)||!isfinite(y)||!isfinite(height)||!isfinite(scale)||!isfinite(rotation)||!isfinite(pixel_size)||pixel_size<0)
        return JS_ThrowRangeError(ctx,"model draw values must be finite; pixel size must be nonnegative");
    ModelDraw draw={.id=id,.x=x,.y=y,.height=height,.scale=scale,.rotation=rotation,.pixel_size=pixel_size,.light_count=-1};
    if(argc>7 && !JS_IsUndefined(argv[7]) && !JS_IsNull(argv[7])) {
        JSValue length=JS_GetPropertyStr(ctx,argv[7],"length"); uint32_t count=0;
        int error=JS_ToUint32(ctx,&count,length); JS_FreeValue(ctx,length);
        if(error)return JS_EXCEPTION;
        if(count>36 || count%9)return JS_ThrowRangeError(ctx,"model lights require at most four 9-value records");
        draw.light_count=(int)(count/9);
        for(uint32_t i=0;i<count;i++) {
            JSValue value=JS_GetPropertyUint32(ctx,argv[7],i); double number;
            error=JS_ToFloat64(ctx,&number,value); JS_FreeValue(ctx,value);
            if(error)return JS_EXCEPTION;
            if(!isfinite(number)||fabs(number)>1e9||(i%9>=3&&number<0))return JS_ThrowRangeError(ctx,"invalid model light value");
            draw.lights[i/9][i%9]=(float)number;
        }
    }
    if(argc>8 && !JS_IsUndefined(argv[8])) {
        if(JS_ToInt32(ctx,&draw.sprite_layer,argv[8]))return JS_EXCEPTION;
        if(draw.sprite_layer<0||draw.sprite_layer>MODEL_INSTANCES+2)return JS_ThrowRangeError(ctx,"invalid sprite layer");
    }
    draw_queue[draw_count++]=draw;
    return JS_UNDEFINED;
}

static void render_model(ModelDraw draw, int pixel) {
    int id=draw.id; double x=draw.x,y=draw.y,height=draw.height,scale=draw.scale,rotation=draw.rotation;
    if(!models.instances[id-1].used)return;
    ModelInstance *in=&models.instances[id-1];ModelAsset *a=&models.assets[in->asset];
    float aspect=(float)sapp_width()/(float)sapp_height();hmm_mat4 projection=pixel?HMM_Orthographic(-MODEL_PIXEL_TARGET/2.0f,MODEL_PIXEL_TARGET/2.0f,-MODEL_PIXEL_TARGET/2.0f,MODEL_PIXEL_TARGET/2.0f,-128,128):HMM_Orthographic(-500*aspect,500*aspect,-500,500,-2000,2000);hmm_mat4 camera=mat_mul(HMM_Rotate(models.pitch,HMM_Vec3(1,0,0)),HMM_Rotate(models.yaw,HMM_Vec3(0,1,0)));float anchor_t[3]={(float)x,(float)-y+(float)height,0},identity_r[4]={0,0,0,1},unit_s[3]={1,1,1};hmm_mat4 anchor=trs(anchor_t,identity_r,unit_s);float local_t[3]={0,0,0},rr[4]={0,sinf((float)rotation*.5f),0,cosf((float)rotation*.5f)},ss[3]={(float)scale,(float)scale,(float)scale};hmm_mat4 root=trs(local_t,rr,ss);
    for(uint32_t ni=0;ni<a->node_count;ni++){ModelNode *n=&a->nodes[ni];for(uint32_t j=0;j<n->primitive_count;j++){ModelPrimitive *p=&a->primitives[n->primitive_start+j];ModelMaterial *m=&a->materials[p->material<a->material_count?p->material:0];hmm_mat4 model=mat_mul(camera,mat_mul(root,in->world[ni]));hmm_mat4 mvp=mat_mul(projection,mat_mul(anchor,model));model_vs_params_t vs={.mvp=mvp,.model=model};model_fs_params_t fs={{m->factor[0],m->factor[1],m->factor[2],m->factor[3]},{models.light_dir[0],models.light_dir[1],models.light_dir[2],models.steps},{models.ambient,m->alpha==MODEL_ALPHA_MASK?(float)1:0,m->cutoff,m->uv_rotation},{m->uv_offset[0],m->uv_offset[1],m->uv_scale[0],m->uv_scale[1]}};
        fs.point_params[0]=(float)draw.light_count;
        for(int light=0;light<draw.light_count;light++) {
            float *l=draw.lights[light];
            hmm_vec4 position=HMM_MultiplyMat4ByVec4(camera,HMM_Vec4(l[0],l[1],l[2],1));
            fs.point_position_radius[light][0]=position.X;
            fs.point_position_radius[light][1]=position.Y;
            fs.point_position_radius[light][2]=position.Z;
            fs.point_position_radius[light][3]=l[4];
            for(int channel=0;channel<3;channel++)fs.point_color_inner[light][channel]=l[5+channel]*l[8];
            fs.point_color_inner[light][3]=l[3];
        }
        sg_apply_pipeline(models.pipelines[m->double_sided?1:0][pixel][m->alpha]);sg_bindings bind={0};bind.vertex_buffers[0]=p->vertices;bind.index_buffer=p->indices;bind.views[VIEW_base_color_tex]=m->view.id?m->view:models.white_view;bind.samplers[SMP_base_color_smp]=models.sampler;sg_apply_bindings(&bind);sg_apply_uniforms(UB_model_vs_params,&SG_RANGE(vs));sg_apply_uniforms(UB_model_fs_params,&SG_RANGE(fs));sg_draw(0,(int)p->index_count,1);}}
}

JSValue js_models_set_last_layer(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    int layer;
    if(argc<1||JS_ToInt32(ctx,&layer,argv[0]))return JS_EXCEPTION;
    if(layer<0||layer>MODEL_INSTANCES+2)return JS_ThrowRangeError(ctx,"invalid final sprite layer");
    world_sprite_last_layer=layer;
    return JS_UNDEFINED;
}

// Called between passes, never inside the sprite pass. All sprite commands
// have already been uploaded once; foreground sprites are drawn after this.
static void begin_model_composite(void) {
    sg_begin_pass(&(sg_pass){.swapchain=sglue_swapchain(),.action={
        .colors[0].load_action=SG_LOADACTION_LOAD,
        // Props are painter-sorted in 2D; only their own triangles share depth.
        .depth={.load_action=SG_LOADACTION_CLEAR,.clear_value=1.0f},
        .stencil.load_action=SG_LOADACTION_LOAD}});
}

void models_render(void) {
    for(int i=0;i<draw_count;i++) {
        ModelDraw draw=draw_queue[i];
        if(draw.sprite_layer>=2) {
            begin_model_composite();
            sgl_draw_layer(draw.sprite_layer);
            sg_end_pass();
        }
        if(!models.pixel_enabled || draw.pixel_size==0) {
            begin_model_composite();
            render_model(draw,0);
            sg_end_pass();
            continue;
        }
        // One source texel equals one source sprite pixel. Room chests supply
        // the snapped world's scale, not the model's size or display DPI.
        double zoom=draw.pixel_size*sapp_height()/1000.0;
        int extent=(int)fmax(1.0,round(MODEL_PIXEL_TARGET*zoom));
        int left=(int)round(sapp_width()*0.5+draw.x*sapp_height()/1000.0-extent*0.5);
        int top=(int)round(sapp_height()*0.5+draw.y*sapp_height()/1000.0-extent*0.5);
        if(left>=sapp_width()||top>=sapp_height()||left+extent<=0||top+extent<=0)continue;
        sg_begin_pass(&(sg_pass){.attachments=models.pixel_attachments,.action={
            .colors[0]={.load_action=SG_LOADACTION_CLEAR,.store_action=SG_STOREACTION_STORE,.clear_value={0,0,0,0}},
            .colors[1]={.load_action=SG_LOADACTION_CLEAR,.store_action=SG_STOREACTION_STORE,.clear_value={0,0,0,0}},
            .depth={.load_action=SG_LOADACTION_CLEAR,.clear_value=1.0f}},.label="128x128 model pixels"});
        ModelDraw local=draw;
        local.x=local.y=0;
        local.scale/=draw.pixel_size;
        local.height/=draw.pixel_size;
        for(int light=0;light<local.light_count;light++)
            for(int component=0;component<5;component++)local.lights[light][component]/=(float)draw.pixel_size;
        render_model(local,1);
        sg_end_pass();

        begin_model_composite();
        sg_apply_viewport(left,top,extent,extent,true);
        sg_apply_pipeline(models.composite_pipeline);
        sg_bindings bindings={0};
        bindings.views[VIEW_model_color]=models.pixel_color_view;
        bindings.samplers[SMP_pixel_sampler]=models.pixel_sampler;
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_pixel_style_params,&SG_RANGE(models.pixel_style));
        sg_draw(0,3,1);
        sg_end_pass();
    }
    if(world_sprite_last_layer>=2) {
        begin_model_composite();
        sgl_draw_layer(world_sprite_last_layer);
        sg_end_pass();
    }
    world_sprite_last_layer=0;
    draw_count=0;
}
