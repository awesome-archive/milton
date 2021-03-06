// Copyright (c) 2015-2016 Sergio Gonzalez. All rights reserved.
// License: https://github.com/serge-rgb/milton#license

#include "shaders.gen.h"

#if MILTON_DEBUG
#define uniform
#define attribute
#define varying
#define in
#define out
#define flat
#define layout(param)
#define main vertexShaderMain
vec4 gl_Position;
vec2 as_vec2(ivec2 v)
{
    vec2 r;
    r.x = v.x;
    r.y = v.y;
    return r;
}
ivec2 as_ivec2(vec2 v)
{
    ivec2 r;
    r.x = v.x;
    r.y = v.y;
    return r;
}
ivec3 as_ivec3(vec3 v)
{
    ivec3 r;
    r.x = v.x;
    r.y = v.y;
    r.z = v.z;
    return r;
}
vec3 as_vec3(ivec3 v)
{
    vec3 r;
    r.x = v.x;
    r.y = v.y;
    r.z = v.z;
    return r;
}
vec4 as_vec4(ivec3 v)
{
    vec4 r;
    r.x = v.x;
    r.y = v.y;
    r.z = v.z;
    r.w = 1;
    return r;
}
vec4 as_vec4(vec3 v)
{
    vec4 r;
    r.x = v.x;
    r.y = v.y;
    r.z = v.z;
    r.w = 1;
    return r;
}
vec2 VEC2(ivec2 v)
{
    vec2 r;
    r.x = v.x;
    r.y = v.y;
    return r;
}
vec2 VEC2(float x,float y)
{
    vec2 r;
    r.x = x;
    r.y = y;
    return r;
}
ivec3 IVEC3(i32 x,i32 y,i32 z)
{
    ivec3 r;
    r.x = x;
    r.y = y;
    r.z = z;
    return r;
}
vec3 VEC3(float v)
{
    vec3 r;
    r.x = v;
    r.y = v;
    r.z = v;
    return r;
}
vec3 VEC3(float x,float y,float z)
{
    vec3 r;
    r.x = x;
    r.y = y;
    r.z = z;
    return r;
}
ivec4 IVEC4(i32 x, i32 y, i32 z, i32 w)
{
    ivec4 r;
    r.x = x;
    r.y = y;
    r.z = z;
    r.w = w;
    return r;
}
vec4 VEC4(float v)
{
    vec4 r;
    r.x = v;
    r.y = v;
    r.z = v;
    r.w = v;
    return r;
}
vec4 VEC4(float x,float y,float z,float w)
{
    vec4 r;
    r.x = x;
    r.y = y;
    r.z = z;
    r.w = w;
    return r;
}
float distance(vec2 a, vec2 b)
{
    float dx = fabs(a.x-b.x);
    float dy = fabs(a.y-b.y);
    float d = dx*dx + dy*dy;
    if (d > 0) d = sqrtf(d);
    return d;
}

static vec2 gl_PointCoord;
static vec4 gl_FragColor;

#pragma warning (push)
#pragma warning (disable : 4668)
#pragma warning (disable : 4200)
#define buffer struct
//#include "common.glsl"
//#include "milton_canvas.v.glsl"
#undef main
#define main fragmentShaderMain
#define texture2D(a,b) VEC4(0)
#define sampler2D int
//#include "milton_canvas.f.glsl"
#pragma warning (pop)
#undef texture2D
#undef main
#undef attribute
#undef uniform
#undef buffer
#undef varying
#undef in
#undef out
#undef flat
#undef sampler2D
#endif //MILTON_DEBUG

// Milton GPU renderer.
//
//
// Rough outline:
//
// The vertex shader rasterizes bounding slabs for each segment of the stroke
//  a    b      c    d
// o-----o------o----o (a four point stroke)
//
//    ___
// a|  / | b   the stroke gets drawn within slabs ab, bc, cd,
//  |/__|
//
//
// The vertex shader: Translate from raster to canvas (i.e. do zoom&panning).
//
// The pixel shader.
//
//      - Check distance to line. (1) get closest point. (2) euclidean dist.
//      (3) brush function
//      - If it is inside, blend color.
//
//
//
//
// == Future?
//
// 1. Sandwich buffers.
//
//      We can render to two textures. One for strokes below the working stroke
//      and one for strokes above. As a final canvas rendering pass, we render
//      the working stroke and blend it between the two textures.
//      This would make the common case for rendering much faster.
//

#define PRESSURE_RESOLUTION (1<<20)

struct TextureUnitID
{
    GLenum opengl_id;
    GLint id;
};

static TextureUnitID g_texture_unit_canvas = { GL_TEXTURE2, 2 };
static TextureUnitID g_texture_unit_output = { GL_TEXTURE1, 1 };

struct RenderData
{
    GLuint stroke_program;
    GLuint blend_program;
    GLuint quad_program;
    GLuint background_program;
    GLuint picker_program;
    GLuint layer_program;

    // VBO for the screen-covering quad.
    GLuint vbo_quad;
    GLuint vbo_quad_uv;  // TODO: Remove and use gl_FragCoord?

    GLuint vbo_picker;
    GLuint vbo_picker_norm;

    GLuint canvas_texture;
    GLuint output_buffer;
    GLuint stencil_texture;

    GLuint fbo;

    b32 gui_visible;

    DArray<RenderElement> render_elems;

    i32 width;
    i32 height;

    v3f background_color;
};

// Load a shader and append line `#version 120`, which is invalid C++
#if MILTON_DEBUG
char* debug_load_shader_from_file(PATH_CHAR* path, size_t* out_size)
{
    char* contents = NULL;
    FILE* common_fd = platform_fopen(TO_PATH_STR("src/common.glsl"), TO_PATH_STR("r"));
    mlt_assert(common_fd);

    size_t bytes_in_common = bytes_in_fd(common_fd);

    char* common_contents = (char*)mlt_calloc(1, bytes_in_common);

    fread(common_contents, 1, bytes_in_common, common_fd);


    FILE* fd = platform_fopen(path, TO_PATH_STR("r"));

    if (fd)
    {
        char prelude[] = "#version 150\n";
        size_t prelude_len = strlen(prelude);
        size_t common_len = strlen(common_contents);
        size_t len = bytes_in_fd(fd);
        contents = (char*)mlt_calloc(len + 1 + common_len + prelude_len, 1);
        if (contents)
        {
            strcpy(contents, prelude);
            strcpy(contents+strlen(contents), common_contents);
            mlt_free(common_contents);

            char* file_data = (char*)mlt_calloc(len,1);
            size_t read = fread((void*)(file_data), 1, (size_t)len, fd);
            file_data[read] = '\0';
            strcpy(contents+strlen(contents), file_data);
            mlt_free(file_data);
            mlt_assert (read <= len);
            fclose(fd);
            if (out_size)
            {
                *out_size = strlen(contents)+1;
            }
            contents[*out_size] = '\0';
        }
    }
    else
    {
        if (out_size)
        {
            *out_size = 0;
        }
    }
    return contents;
}
#endif

static void print_framebuffer_status()
{
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    char* msg = NULL;
    switch (status)
    {
        case GL_FRAMEBUFFER_COMPLETE:
        {
            // OK!
            break;
        }
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        {
            msg = "Incomplete Attachment";
            break;
        }
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        {
            msg = "Missing Attachment";
            break;
        }
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        {
            msg = "Incomplete Draw Buffer";
            break;
        }
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        {
            msg = "Incomplete Read Buffer";
            break;
        }
        case GL_FRAMEBUFFER_UNSUPPORTED:
        {
            msg = "Unsupported Framebuffer";
            break;
        }
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        {
            msg = "Incomplete Multisample";
            break;
        }
        default:
        {
            msg = "Unknown";
            break;
        }
    }

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        char warning[1024];
        snprintf(warning, "Framebuffer Error: %s", msg);
        milton_log("Warning %s\n", warning);
    }
}

void gpu_update_picker(RenderData* render_data, ColorPicker* picker)
{
    glUseProgram(render_data->picker_program);
    // Transform to [-1,1]
    v2f a = picker->data.a;
    v2f b = picker->data.b;
    v2f c = picker->data.c;
    // TODO include my own lambda impl.
    Rect bounds = picker_get_bounds(picker);
    int w = bounds.right-bounds.left;
    int h = bounds.bottom-bounds.top;
    // The center of the picker has an offset of (20,30)
    // and the bounds radius is 100 px
    auto transform = [&](v2f p) { return v2f{2*p.x/w-1 - 0.20f, 2*p.y/h-1 -0.30f}; };
    a = transform(a);
    b = transform(b);
    c = transform(c);
    gl_set_uniform_vec2(render_data->picker_program, "u_pointa", 1, a.d);
    gl_set_uniform_vec2(render_data->picker_program, "u_pointb", 1, b.d);
    gl_set_uniform_vec2(render_data->picker_program, "u_pointc", 1, c.d);
    gl_set_uniform_f(render_data->picker_program, "u_angle", picker->data.hsv.h);

    v3f hsv = picker->data.hsv;
    gl_set_uniform_vec3(render_data->picker_program, "u_color", 1, hsv_to_rgb(hsv).d);

    // Point within triangle
    {
        // Barycentric to cartesian
        f32 fa = hsv.s;
        f32 fb = 1 - hsv.v;
        f32 fc = 1 - fa - fb;

        v2f point = add2f(add2f((scale2f(picker->data.c,fa)), scale2f(picker->data.b,fb)), scale2f(picker->data.a,fc));
        // Move to [-1,1]^2
        point = transform(point);
        gl_set_uniform_vec2(render_data->picker_program, "u_triangle_point", 1, point.d);
    }
    v4f colors[5] = {};
    ColorButton* button = &picker->color_buttons;
    colors[0] = button->rgba; button = button->next;
    colors[1] = button->rgba; button = button->next;
    colors[2] = button->rgba; button = button->next;
    colors[3] = button->rgba; button = button->next;
    colors[4] = button->rgba; button = button->next;
    gl_set_uniform_vec4(render_data->picker_program, "u_colors", 5, (float*)colors);

    // Update VBO for picker
    {
        Rect rect = get_bounds_for_picker_and_colors(picker);
        // convert to clip space
        v2i screen_size = {render_data->width / SSAA_FACTOR, render_data->height / SSAA_FACTOR};
        float top = (float)rect.top / screen_size.h;
        float bottom = (float)rect.bottom / screen_size.h;
        float left = (float)rect.left / screen_size.w;
        float right = (float)rect.right / screen_size.w;
        top = (top*2.0f - 1.0f) * -1;
        bottom = (bottom*2.0f - 1.0f) *-1;
        left = left*2.0f - 1.0f;
        right = right*2.0f - 1.0f;
        // a------d
        // |  \   |
        // |    \ |
        // b______c
        GLfloat data[] =
        {
            left, top,
            left, bottom,
            right, bottom,
            right, top,
        };
        float ratio = (float)(rect.bottom-rect.top) / (float)(rect.right-rect.left);
        ratio = (ratio*2)-1;
        // normalized positions.
        GLfloat norm[] =
        {
            -1, -1,
            -1, ratio,
            1, ratio,
            1, -1,
        };

        // Create buffers and upload
        glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_picker);
        glBufferData(GL_ARRAY_BUFFER, array_count(data)*sizeof(*data), data, GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_picker_norm);
        glBufferData(GL_ARRAY_BUFFER, array_count(norm)*sizeof(*norm), norm, GL_STATIC_DRAW);
    }
}

b32 is_layer(RenderElement* render_element)
{
    b32 result = render_element->count == RenderElementType_LAYER;
    return result;
}

b32 gpu_init(RenderData* render_data, CanvasView* view, ColorPicker* picker)
{
    glEnable(GL_SAMPLE_SHADING_ARB);
    glMinSampleShadingARB(1.0);
    //mlt_assert(PRESSURE_RESOLUTION == PRESSURE_RESOLUTION_GL);
    // TODO: Handle this. New MLT version?
    // mlt_assert(STROKE_MAX_POINTS == STROKE_MAX_POINTS_GL);

    bool result = true;

    render_data->gui_visible = true;

#if MILTON_DEBUG
    // Assume our context is 3.0+
    // Create a single VAO and bind it.
    GLuint proxy_vao = 0;
    GLCHK( glGenVertexArrays(1, &proxy_vao) );
    GLCHK( glBindVertexArray(proxy_vao) );
#endif

    // Load shader into opengl.
    {
        GLuint objs[2];

        // TODO: In release, include the code directly.
#if MILTON_DEBUG
        size_t src_sz[2] = {0};
        char* src[2] =
        {
            debug_load_shader_from_file(TO_PATH_STR("src/milton_canvas.v.glsl"), &src_sz[0]),
            debug_load_shader_from_file(TO_PATH_STR("src/milton_canvas.f.glsl"), &src_sz[1]),
        };
        GLuint types[2] =
        {
            GL_VERTEX_SHADER,
            GL_FRAGMENT_SHADER
        };
#endif
        result = src_sz[0] != 0 && src_sz[1] != 0;

        mlt_assert(array_count(src) == array_count(objs));
        for (i64 i=0; i < array_count(src); ++i)
        {
            objs[i] = gl_compile_shader(src[i], types[i]);
        }

        render_data->stroke_program = glCreateProgram();

        gl_link_program(render_data->stroke_program, objs, array_count(objs));

        GLCHK( glUseProgram(render_data->stroke_program) );
    }
    { // blend program
#if MILTON_DEBUG
        GLuint objs[2] = {};
        size_t vsz, fsz;
        char* vsrc = debug_load_shader_from_file(TO_PATH_STR("src/blend.v.glsl"), &vsz);
        char* fsrc = debug_load_shader_from_file(TO_PATH_STR("src/blend.f.glsl"), &fsz);
#endif

        objs[0] = gl_compile_shader(vsrc, GL_VERTEX_SHADER);
        objs[1] = gl_compile_shader(fsrc, GL_FRAGMENT_SHADER);

        render_data->blend_program = glCreateProgram();
        gl_link_program(render_data->blend_program, objs, array_count(objs));
        GLCHK( glUseProgram(render_data->blend_program) );
    }

    gl_set_uniform_i(render_data->blend_program, "u_canvas", g_texture_unit_canvas.id);
    gl_set_uniform_i(render_data->stroke_program, "u_canvas", g_texture_unit_canvas.id);

    // Quad for screen!
    {
        // a------d
        // |  \   |
        // |    \ |
        // b______c
        //  Triangle fan:
        GLfloat quad_data[] =
        {
            -1 , -1 , // a
            -1 , 1  , // b
            1  , 1  , // c
            1  , -1 , // d
        };

        // Create buffers and upload
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, array_count(quad_data)*sizeof(*quad_data), quad_data, GL_STATIC_DRAW);

        float u = 1.0f;
        GLfloat uv_data[] =
        {
            0,0,
            0,u,
            u,u,
            u,0,
        };
        GLuint vbo_uv = 0;
        glGenBuffers(1, &vbo_uv);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_uv);
        glBufferData(GL_ARRAY_BUFFER, array_count(uv_data)*sizeof(*uv_data), uv_data, GL_STATIC_DRAW);


        render_data->vbo_quad = vbo;
        render_data->vbo_quad_uv = vbo_uv;

        char vsrc[] =
        "#version 120 \n"
        "attribute vec2 a_point; \n"
        "attribute vec2 a_uv; \n"
        "varying vec2 v_uv; \n"
        "void main() { \n"
        "v_uv = a_uv; \n"
        "    gl_Position = vec4(a_point, 0,1); \n"
        "} \n";
        char fsrc[] =
        "#version 120 \n"
        "uniform sampler2D u_canvas; \n"
        "varying vec2 v_uv; \n"
        "void main() \n"
        "{ \n"
        "vec4 color = texture2D(u_canvas, v_uv); \n"
        "gl_FragColor = color; \n"
        "} \n";

        GLuint objs[2] = {};
        objs[0] = gl_compile_shader(vsrc, GL_VERTEX_SHADER);
        objs[1] = gl_compile_shader(fsrc, GL_FRAGMENT_SHADER);
        render_data->quad_program = glCreateProgram();
        gl_link_program(render_data->quad_program, objs, array_count(objs));
    }
    // Background fill program
    {
        render_data->background_program = glCreateProgram();

        char vsrc[] =
        "#version 120 \n"
        "attribute vec2 a_point; \n"
        "void main() \n"
        "{ \n"
        "    gl_Position = vec4(a_point, 0,1); \n"
        "} \n";
        char fsrc[] =
        "#version 120 \n"
        "uniform vec3 u_background_color;"
        "void main() \n"
        "{ \n"
        "gl_FragColor = vec4(u_background_color, 0); \n"
        "} \n";
        GLuint objs[2] = {};
        objs[0] = gl_compile_shader(vsrc, GL_VERTEX_SHADER);
        objs[1] = gl_compile_shader(fsrc, GL_FRAGMENT_SHADER);
        gl_link_program(render_data->background_program, objs, array_count(objs));
    }
    {  // Color picker shader
        render_data->picker_program = glCreateProgram();
        GLuint objs[2] = {};

        // g_picker_* gnerated by shadergen.cc
        objs[0] = gl_compile_shader(g_picker_v, GL_VERTEX_SHADER);
        objs[1] = gl_compile_shader(g_picker_f, GL_FRAGMENT_SHADER);
        gl_link_program(render_data->picker_program, objs, array_count(objs));
    }
    {  // Layer blend shader
        render_data->layer_program = glCreateProgram();
        GLuint objs[2] = {};

        objs[0] = gl_compile_shader(g_layer_blend_v, GL_VERTEX_SHADER);
        objs[1] = gl_compile_shader(g_layer_blend_f, GL_FRAGMENT_SHADER);
        gl_link_program(render_data->layer_program, objs, array_count(objs));
        gl_set_uniform_i(render_data->layer_program, "u_canvas", g_texture_unit_canvas.id);
    }

    // Framebuffer object for canvas. Layer buffer
    {
        glActiveTexture(g_texture_unit_canvas.opengl_id);
        GLCHK (glGenTextures(1, &render_data->canvas_texture));
        glBindTexture(GL_TEXTURE_2D, render_data->canvas_texture);

        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        glTexImage2D(GL_TEXTURE_2D,
         0, GL_RGBA,
         view->screen_size.w, view->screen_size.h,
         0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, render_data->canvas_texture);


        glBindTexture(GL_TEXTURE_2D, 0);

        glActiveTexture(g_texture_unit_output.opengl_id);
        GLCHK (glGenTextures(1, &render_data->output_buffer));
        glBindTexture(GL_TEXTURE_2D, render_data->output_buffer);

        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        glTexImage2D(GL_TEXTURE_2D,
         0, GL_RGBA,
         view->screen_size.w, view->screen_size.h,
         0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


        glBindTexture(GL_TEXTURE_2D, 0);

        glGenTextures(1, &render_data->stencil_texture);
        glBindTexture(GL_TEXTURE_2D, render_data->stencil_texture);


        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GLCHK (glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        /* glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY); */
        /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE); */
        /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL); */
        GLCHK( glTexImage2D(GL_TEXTURE_2D, 0,
                            /*internalFormat, num of components*/GL_DEPTH24_STENCIL8,
            view->screen_size.w, view->screen_size.h,
                            /*border*/0, /*pixel_data_format*/GL_DEPTH_STENCIL,
                            /*component type*/GL_UNSIGNED_INT_24_8,
            NULL) );


        glBindTexture(GL_TEXTURE_2D, 0);
        /* GLuint rbo = 0; */
        /* glGenRenderbuffers(1, &rbo); */
        /* glBindRenderbuffer(GL_RENDERBUFFER, rbo); */
        /* glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, view->screen_size.w, view->screen_size.h); */
        //GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MUL, stencil_texture, 0) );

#if 0
        int depth_size;
        GLCHK( glGetIntegerv(GL_DEPTH_BITS, &depth_size) );
        int stencil_size;
        GLCHK( glGetIntegerv(GL_STENCIL_BITS, &stencil_size) );
#endif

        {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                render_data->canvas_texture, 0) );
            GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                render_data->stencil_texture, 0) );
            render_data->fbo = fbo;
            print_framebuffer_status();
        }
        GLCHK( glBindFramebuffer(GL_FRAMEBUFFER, 0) );
    }
    // VBO for picker
    glGenBuffers(1, &render_data->vbo_picker);
    glGenBuffers(1, &render_data->vbo_picker_norm);

    // Call gpu_update_picker() to initialize the color picker
    gpu_update_picker(render_data, picker);
    return result;
}

void gpu_resize(RenderData* render_data, CanvasView* view)
{
    render_data->width = view->screen_size.w;
    render_data->height = view->screen_size.h;
    i32 tex_w = view->screen_size.w;
    i32 tex_h = view->screen_size.h;
    // Create canvas texture

    glActiveTexture(g_texture_unit_output.opengl_id);
    GLCHK (glBindTexture(GL_TEXTURE_2D, render_data->output_buffer));
    GLCHK (glTexImage2D(GL_TEXTURE_2D, 0, /*internalFormat, num of components*/GL_RGBA8,
                        tex_w, tex_h,
                        /*border*/0, /*pixel_data_format*/GL_BGRA,
                        /*component type*/GL_UNSIGNED_BYTE, NULL));
    glActiveTexture(g_texture_unit_canvas.opengl_id);
    GLCHK (glBindTexture(GL_TEXTURE_2D, render_data->canvas_texture));
    GLCHK (glTexImage2D(GL_TEXTURE_2D, 0, /*internalFormat, num of components*/GL_RGBA8,
                        tex_w, tex_h,
                        /*border*/0, /*pixel_data_format*/GL_BGRA,
                        /*component type*/GL_UNSIGNED_BYTE, NULL));


    glBindTexture(GL_TEXTURE_2D, render_data->stencil_texture);
    GLCHK( glTexImage2D(GL_TEXTURE_2D, 0,
                        /*internalFormat, num of components*/GL_DEPTH24_STENCIL8,
                        tex_w, tex_h,
                        /*border*/0, /*pixel_data_format*/GL_DEPTH_STENCIL,
                        /*component type*/GL_UNSIGNED_INT_24_8,
                        NULL) );
}

void gpu_update_scale(RenderData* render_data, i32 scale)
{
#if MILTON_DEBUG // set the shader values in C++
    // Shader
    // u_scale = scale;
#endif
    gl_set_uniform_i(render_data->stroke_program, "u_scale", scale);
    gl_set_uniform_i(render_data->blend_program, "u_scale", scale);
}

static void gpu_set_background(RenderData* render_data, v3f background_color)
{
#if MILTON_DEBUG
    // SHADER
    // for(int i=0;i<3;++i) u_background_color.d[i] = background_color.d[i];
#endif
    gl_set_uniform_vec3(render_data->stroke_program, "u_background_color", 1, background_color.d);
    gl_set_uniform_vec3(render_data->blend_program, "u_background_color", 1, background_color.d);
    gl_set_uniform_vec3(render_data->background_program, "u_background_color", 1, background_color.d);

    render_data->background_color = background_color;
}

void gpu_set_canvas(RenderData* render_data, CanvasView* view)
{
#if MILTON_DEBUG // set the shader values in C++
#define COPY_VEC(a,b) a.x = b.x; a.y = b.y;
    // SHADER
    //COPY_VEC( u_pan_vector, view->pan_vector );
    //COPY_VEC( u_screen_center, view->screen_center );
    //COPY_VEC( u_screen_size, view->screen_size );
    //u_scale = view->scale;
#undef COPY_VEC
#endif
    glUseProgram(render_data->stroke_program);

    auto center = divide2i(view->screen_center, 1);
    auto pan = divide2i(view->pan_vector, 1);
    gl_set_uniform_vec2i(render_data->stroke_program, "u_pan_vector", 1, pan.d);
    gl_set_uniform_vec2i(render_data->blend_program, "u_pan_vector", 1, pan.d);
    gl_set_uniform_vec2i(render_data->stroke_program, "u_screen_center", 1, center.d);
    gl_set_uniform_vec2i(render_data->blend_program, "u_screen_center", 1, center.d);
    float fscreen[] = { (float)view->screen_size.x, (float)view->screen_size.y };
    gl_set_uniform_vec2(render_data->stroke_program, "u_screen_size", 1, fscreen);
    gl_set_uniform_vec2(render_data->blend_program, "u_screen_size", 1, fscreen);
    gl_set_uniform_i(render_data->stroke_program, "u_scale", view->scale);
    gl_set_uniform_i(render_data->blend_program, "u_scale", view->scale);
}

void gpu_clip_strokes(RenderData* render_data, Layer* root_layer, Stroke* working_stroke)
{
    auto *render_elements = &render_data->render_elems;

    RenderElement layer_element = {};

    layer_element.count = RenderElementType_LAYER;

    reset(render_elements);
    for(Layer* l = root_layer;
        l != NULL;
        l = l->next)
    {
        // TODO: This loop is ugly and stupid and needs a better way of being expressed.
        for (u64 i = 0; i <= l->strokes.count; ++i)
        {
            Stroke* s = &l->strokes.data[i];
            // Only push the working stroke when the next layer is NULL, which means that we are at the topmost layer.
            if (i == l->strokes.count && l->next == NULL)
            {
                push(render_elements, working_stroke->render_element);
            }
            else if (i < l->strokes.count)
            {
                push(render_elements, s->render_element);
            }
        }

        push(render_elements, layer_element);
    }
}

// TODO: Measure memory consumption of glBufferData and their ilk

enum CookStrokeOpt
{
    CookStroke_NEW                   = 0,
    CookStroke_UPDATE_WORKING_STROKE = 1,
};
void gpu_cook_stroke(Arena* arena, RenderData* render_data, Stroke* stroke, CookStrokeOpt cook_option = CookStroke_NEW)
{
    if (cook_option == CookStroke_NEW && stroke->render_element.vbo_stroke != 0)
    {
        // We already have our data cooked
        mlt_assert(stroke->render_element.vbo_pointa != 0);
        mlt_assert(stroke->render_element.vbo_pointb != 0);
    }
    else
    {
        vec2 cp;
        cp.x = stroke->points[stroke->num_points-1].x;
        cp.y = stroke->points[stroke->num_points-1].y;
#if MILTON_DEBUG
        // SHADER
        //if (u_scale != 0)
        //{
        //    canvas_to_raster_gl(cp);
        //}
#endif

        auto npoints = stroke->num_points;
        if (npoints == 1)
        {
            // TODO: handle special case...
            // So uncommon that adding an extra degenerate point might make sense,
            // if the algorithm can handle it

        }
        else if (npoints > 1)
        {
            GLCHK( glUseProgram(render_data->stroke_program) );

            // 3 (triangle) *
            // 2 (two per segment) *
            // N-1 (segments per stroke)
            const size_t count_bounds = 3*2*((size_t)npoints-1);

            // 6 (3 * 2 from count_bounds)
            // N-1 (num segments)
            const size_t count_points = 6*((size_t)npoints-1);

            v2i* bounds;
            v3i* apoints;
            v3i* bpoints;
            Arena scratch_arena = arena_push(arena, count_bounds*sizeof(decltype(*bounds))
             + 2*count_points*sizeof(decltype(*apoints)));

            bounds  = arena_alloc_array(&scratch_arena, count_bounds, v2i);
            apoints = arena_alloc_array(&scratch_arena, count_bounds, v3i);
            bpoints = arena_alloc_array(&scratch_arena, count_bounds, v3i);

            size_t bounds_i = 0;
            size_t apoints_i = 0;
            size_t bpoints_i = 0;
            for (i64 i=0; i < npoints-1; ++i)
            {
                v2i point_i = stroke->points[i];
                v2i point_j = stroke->points[i+1];

                Brush brush = stroke->brush;
                float radius_i = stroke->pressures[i]*brush.radius;
                float radius_j = stroke->pressures[i+1]*brush.radius;

                i32 min_x = min(point_i.x-radius_i, point_j.x-radius_j);
                i32 min_y = min(point_i.y-radius_i, point_j.y-radius_j);

                i32 max_x = max(point_i.x+radius_i, point_j.x+radius_j);
                i32 max_y = max(point_i.y+radius_i, point_j.y+radius_j);

                // Bounding rect
                //  Counter-clockwise
                //  TODO: Use triangle strips and flat shading to reduce redundant data
                bounds[bounds_i++] = { min_x, min_y };
                bounds[bounds_i++] = { min_x, max_y };
                bounds[bounds_i++] = { max_x, max_y };

                bounds[bounds_i++] = { max_x, max_y };
                bounds[bounds_i++] = { min_x, min_y };
                bounds[bounds_i++] = { max_x, min_y };

                // Pressures are in (0,1] but we need to encode them as integers.
                i32 pressure_a = (i32)(stroke->pressures[i] * (float)(PRESSURE_RESOLUTION));
                i32 pressure_b = (i32)(stroke->pressures[i+1] * (float)(PRESSURE_RESOLUTION));

                // one for every point in the triangle.
                for (int repeat = 0; repeat < 6; ++repeat)
                {
                    apoints[apoints_i++] = { point_i.x, point_i.y, pressure_a };
                    bpoints[bpoints_i++] = { point_j.x, point_j.y, pressure_b };
                }
            }

            mlt_assert(bounds_i == count_bounds);

            // TODO: check for GL_OUT_OF_MEMORY

            GLuint vbo_stroke = 0;
            GLuint vbo_pointa = 0;
            GLuint vbo_pointb = 0;


            GLenum hint = GL_STATIC_DRAW;
            if (cook_option == CookStroke_UPDATE_WORKING_STROKE)
            {
                hint = GL_DYNAMIC_DRAW;
            }
            if (stroke->render_element.vbo_stroke == 0)  // Cooking the stroke for the first time.
            {
                glGenBuffers(1, &vbo_stroke);
                glGenBuffers(1, &vbo_pointa);
                glGenBuffers(1, &vbo_pointb);
                glBindBuffer(GL_ARRAY_BUFFER, vbo_stroke);
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*bounds))), bounds, hint) );
                glBindBuffer(GL_ARRAY_BUFFER, vbo_pointa);
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*apoints))), apoints, hint) );
                glBindBuffer(GL_ARRAY_BUFFER, vbo_pointb);
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*bpoints))), bpoints, hint) );
            }
            else  // Updating the working stroke
            {
                vbo_stroke = stroke->render_element.vbo_stroke;
                vbo_pointa = stroke->render_element.vbo_pointa;
                vbo_pointb = stroke->render_element.vbo_pointb;

                glBindBuffer(GL_ARRAY_BUFFER, vbo_stroke);
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*bounds))), NULL, hint) );
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*bounds))), bounds, hint) );
                glBindBuffer(GL_ARRAY_BUFFER, vbo_pointa);
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*apoints))), NULL, hint) );
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*apoints))), apoints, hint) );
                glBindBuffer(GL_ARRAY_BUFFER, vbo_pointb);
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*bpoints))), NULL, hint) );
                GLCHK( glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bounds_i*sizeof(decltype(*bpoints))), bpoints, hint) );
            }

            GLuint ubo = 0;

            RenderElement re = {};
            re.vbo_stroke = vbo_stroke;
            re.vbo_pointa = vbo_pointa;
            re.vbo_pointb = vbo_pointb;
            re.count = (i64)bounds_i;
            re.color = { stroke->brush.color.r, stroke->brush.color.g, stroke->brush.color.b, stroke->brush.color.a };
            re.radius = stroke->brush.radius;
            mlt_assert(re.count > 1);

            stroke->render_element = re;

            arena_pop(&scratch_arena);
        }
    }
}

void gpu_render(RenderData* render_data)
{
    glViewport(0,0, render_data->width, render_data->height);
    glScissor(0,0, render_data->width, render_data->height);
    GLCHK( glBindFramebuffer(GL_FRAMEBUFFER, render_data->fbo) );
    print_framebuffer_status();

    GLCHK( glActiveTexture(g_texture_unit_canvas.opengl_id) );
    GLCHK( glBindTexture(GL_TEXTURE_2D, render_data->output_buffer) );
    GLCHK( glClearColor(render_data->background_color.r, render_data->background_color.g, render_data->background_color.b, 0.0f) );
    GLCHK( glClear(GL_COLOR_BUFFER_BIT) );
    GLCHK( glBindTexture(GL_TEXTURE_2D, render_data->canvas_texture) );
    // Render background color
    {
        GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_data->output_buffer, 0) );
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(render_data->background_program);

        GLint loc = glGetAttribLocation(render_data->background_program, "a_point");
        if (loc >= 0)
        {
            GLCHK( glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_quad) );
            GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc,
                                         /*size*/2, GL_FLOAT, /*normalize*/GL_FALSE,
                                         /*stride*/0, /*ptr*/0));
            glEnableVertexAttribArray((GLuint)loc);
            GLCHK( glDrawArrays(GL_TRIANGLE_FAN,0,4) );
        }
    }
    GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_data->canvas_texture, 0) );
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    // Render strokes
    {
        GLCHK( glUseProgram(render_data->stroke_program) );
        GLint loc = glGetAttribLocation(render_data->stroke_program, "a_position");
        GLint loc_a = glGetAttribLocation(render_data->stroke_program, "a_pointa");
        GLint loc_b = glGetAttribLocation(render_data->stroke_program, "a_pointb");
        if (loc >= 0)
        {
            // NOTE: Front to back w/discard is faster without stencil, but slower with stencil.
            //for (i64 i = (i64)render_data->render_elems.count-1; i>=0; --i)
            for (i64 i = 0 ; i <(i64)render_data->render_elems.count; i++)
            {
                GLCHK( glUseProgram(render_data->stroke_program) );
                RenderElement* re = &render_data->render_elems.data[i];

                if (is_layer(re))
                {
#if 0
                    continue;
#else
                    // Layer render element.
                    //  Render to render_data->output_buffer texture, blending the contents of canvas_texture.
                    //  Then clearing canvas_texture.
                    glDisable(GL_STENCIL_TEST);
                    //glEnable(GL_BLEND);
                    //glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                    // Got to the end of a layer.
                    // Blend onto the canvas buffer.
                    glUseProgram(render_data->layer_program);

                    //glBindTexture(GL_TEXTURE_2D, render_data->canvas_texture);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_data->output_buffer, 0);

                    GLint p_loc = glGetAttribLocation(render_data->layer_program, "a_position");
                    if (p_loc >= 0)
                    {
                        GLCHK( glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_quad) );
                        GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)p_loc,
                                                     /*size*/2, GL_FLOAT, /*normalize*/GL_FALSE,
                                                     /*stride*/0, /*ptr*/0));
                        glEnableVertexAttribArray((GLuint)p_loc);

                        GLint uv_loc = glGetAttribLocation(render_data->layer_program, "a_uv");
                        if (uv_loc >= 0)
                        {
                            GLCHK( glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_quad_uv) );
                            GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)uv_loc,
                                                         /*size*/2, GL_FLOAT, /*normalize*/GL_FALSE,
                                                         /*stride*/0, /*ptr*/0));
                            glEnableVertexAttribArray((GLuint)uv_loc);
                        }

                        GLCHK( glDrawArrays(GL_TRIANGLE_FAN,0,4) );
                    }
                    glEnable(GL_STENCIL_TEST);
                    //glDisable(GL_BLEND);

                    GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_data->canvas_texture, 0) );
                    // TODO: uncomment this
                    //glClear(GL_COLOR_BUFFER_BIT);
#endif
                }
                else
                {
                    i64 count = re->count;

                    // TODO. Only set these uniforms when both are different from the ones in use.
                    gl_set_uniform_vec4(render_data->stroke_program, "u_brush_color", 1, re->color.d);
                    gl_set_uniform_vec4(render_data->blend_program, "u_brush_color", 1, re->color.d);
                    gl_set_uniform_i(render_data->stroke_program, "u_radius", re->radius);

                    if (loc_a >=0)
                    {
                        GLCHK( glBindBuffer(GL_ARRAY_BUFFER, re->vbo_pointa) );
#if 0
                        GLCHK( glVertexAttribIPointer(/*attrib location*/(GLuint)loc_a,
                                                      /*size*/3, GL_INT,
                                                      /*stride*/0, /*ptr*/0));
#else
                        GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc_a,
                                                     /*size*/3, GL_INT, /*normalize*/GL_FALSE,
                                                     /*stride*/0, /*ptr*/0));
#endif
                        GLCHK( glEnableVertexAttribArray((GLuint)loc_a) );
                    }
                    if (loc_b >=0)
                    {
                        GLCHK( glBindBuffer(GL_ARRAY_BUFFER, re->vbo_pointb) );
#if 0
                        GLCHK( glVertexAttribIPointer(/*attrib location*/(GLuint)loc_b,
                                                      /*size*/3, GL_INT,
                                                      /*stride*/0, /*ptr*/0));
#else
                        GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc_b,
                                                     /*size*/3, GL_INT, /*normalize*/GL_FALSE,
                                                     /*stride11,059,200*/0, /*ptr*/0));
#endif
                        GLCHK( glEnableVertexAttribArray((GLuint)loc_b) );
                    }


                    GLCHK( glBindBuffer(GL_ARRAY_BUFFER, re->vbo_stroke) );
                    GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc,
                                                 /*size*/2, GL_INT, /*normalize*/GL_FALSE,
                                                 /*stride*/0, /*ptr*/0));
                    GLCHK( glEnableVertexAttribArray((GLuint)loc) );


                    // TODO: Check which one of these is available. And use glMemoryBarrierARB, not EXT
                    glTextureBarrierNV();
                    //glMemoryBarrierEXT(GL_TEXTURE_FETCH_BARRIER_BIT_EXT);

                    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glStencilFunc(GL_ALWAYS,1,0xFF);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    GLCHK( glDrawArrays(GL_TRIANGLES, 0, count) );
                    GLCHK( glUseProgram(render_data->blend_program) );
                    GLint blend_pos_loc = glGetAttribLocation(render_data->blend_program, "a_position");
                    if (blend_pos_loc >= 0)
                    {
                        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#if 0
                        GLint blend_uv_loc = glGetAttribLocation(render_data->blend_program, "a_uv");
                        if (blend_uv_loc >= 0)
                        {
                            glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_quad_uv);
                            GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)blend_uv_loc,
                                                         /*size*/2, GL_INT, /*normalize*/GL_FALSE,
                                                         /*stride*/0, /*ptr*/0));
                            glEnableVertexAttribArray((GLuint)blend_uv_loc);
                        }
#endif

                        GLCHK( glBindBuffer(GL_ARRAY_BUFFER, re->vbo_stroke) );
                        GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)blend_pos_loc,
                                                     /*size*/2, GL_INT, /*normalize*/GL_FALSE,
                                                     /*stride*/0, /*ptr*/0));
                        GLCHK( glEnableVertexAttribArray((GLuint)blend_pos_loc) );
                        glStencilFunc(GL_EQUAL,1,0xFF);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
                        GLCHK( glDrawArrays(GL_TRIANGLES, 0, count) );
                    }
                }
            }
        }
    }
    GLCHK( glDisable(GL_STENCIL_TEST) );

    GLCHK( glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_data->output_buffer, 0) );
    // Render picker
    if (render_data->gui_visible)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(render_data->picker_program);
        GLint loc = glGetAttribLocation(render_data->picker_program, "a_position");

        if (loc >= 0)
        {
            GLCHK( glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_picker) );
            GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc,
                                         /*size*/2, GL_FLOAT, /*normalize*/GL_FALSE,
                                         /*stride*/0, /*ptr*/0));
            glEnableVertexAttribArray((GLuint)loc);
            GLint loc_norm = glGetAttribLocation(render_data->picker_program, "a_norm");
            if (loc_norm >= 0)
            {
                GLCHK( glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_picker_norm) );
                GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc_norm,
                                             /*size*/2, GL_FLOAT, /*normalize*/GL_FALSE,
                                             /*stride*/0, /*ptr*/0));
                glEnableVertexAttribArray((GLuint)loc_norm);

            }
            GLCHK( glDrawArrays(GL_TRIANGLE_FAN,0,4) );
        }
        glDisable(GL_BLEND);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0,0, render_data->width/SSAA_FACTOR, render_data->height/SSAA_FACTOR);
    glScissor(0,0, render_data->width/SSAA_FACTOR, render_data->height/SSAA_FACTOR);
    // Render output buffer
    {
        glUseProgram(render_data->quad_program);
        glActiveTexture(g_texture_unit_canvas.opengl_id);
        glBindTexture(GL_TEXTURE_2D, render_data->output_buffer);

        GLint loc = glGetAttribLocation(render_data->quad_program, "a_point");
        if (loc >= 0)
        {
            GLint loc_uv = glGetAttribLocation(render_data->quad_program, "a_uv");
            if (loc_uv >=0)
            {
                GLCHK( glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_quad_uv) );
                GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc_uv,
                                             /*size*/2, GL_FLOAT, /*normalize*/GL_FALSE,
                                             /*stride*/0, /*ptr*/0));
                glEnableVertexAttribArray((GLuint)loc_uv);
            }

            GLCHK( glBindBuffer(GL_ARRAY_BUFFER, render_data->vbo_quad) );
            GLCHK( glVertexAttribPointer(/*attrib location*/(GLuint)loc,
                                         /*size*/2, GL_FLOAT, /*normalize*/GL_FALSE,
                                         /*stride*/0, /*ptr*/0));
            glEnableVertexAttribArray((GLuint)loc);
            GLCHK( glDrawArrays(GL_TRIANGLE_FAN,0,4) );
        }
    }
    GLCHK (glUseProgram(0));
}

