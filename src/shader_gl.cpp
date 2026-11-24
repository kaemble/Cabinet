#include "shader_pass.h"
#include "color_correction_shaders.h"
#include <SDL3/SDL_opengl.h>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
struct GlPass : ShaderPass
{
#define FUNCTION(type, name) type name = nullptr
    FUNCTION(PFNGLCREATESHADERPROC, create_shader);
    FUNCTION(PFNGLSHADERSOURCEPROC, shader_source);
    FUNCTION(PFNGLCOMPILESHADERPROC, compile_shader);
    FUNCTION(PFNGLGETSHADERIVPROC, get_shader_iv);
    FUNCTION(PFNGLGETSHADERINFOLOGPROC, get_shader_log);
    FUNCTION(PFNGLDELETESHADERPROC, delete_shader);
    FUNCTION(PFNGLCREATEPROGRAMPROC, create_program);
    FUNCTION(PFNGLATTACHSHADERPROC, attach_shader);
    FUNCTION(PFNGLBINDATTRIBLOCATIONPROC, bind_attribute);
    FUNCTION(PFNGLLINKPROGRAMPROC, link_program);
    FUNCTION(PFNGLGETPROGRAMIVPROC, get_program_iv);
    FUNCTION(PFNGLGETPROGRAMINFOLOGPROC, get_program_log);
    FUNCTION(PFNGLDELETEPROGRAMPROC, delete_program);
    FUNCTION(PFNGLUSEPROGRAMPROC, use_program);
    FUNCTION(PFNGLGETUNIFORMLOCATIONPROC, uniform_location);
    FUNCTION(PFNGLUNIFORM4FVPROC, uniform_4fv);
    FUNCTION(PFNGLUNIFORM1IPROC, uniform_1i);
    FUNCTION(PFNGLGENBUFFERSPROC, gen_buffers);
    FUNCTION(PFNGLBINDBUFFERPROC, bind_buffer);
    FUNCTION(PFNGLBUFFERDATAPROC, buffer_data);
    FUNCTION(PFNGLDELETEBUFFERSPROC, delete_buffers);
    FUNCTION(PFNGLVERTEXATTRIBPOINTERPROC, attribute_pointer);
    FUNCTION(PFNGLENABLEVERTEXATTRIBARRAYPROC, enable_attribute);
    FUNCTION(PFNGLDISABLEVERTEXATTRIBARRAYPROC, disable_attribute);
    FUNCTION(PFNGLGETVERTEXATTRIBIVPROC, get_attribute);
    FUNCTION(PFNGLGETVERTEXATTRIBPOINTERVPROC, get_attribute_pointer);
    FUNCTION(PFNGLGENFRAMEBUFFERSPROC, gen_framebuffers);
    FUNCTION(PFNGLBINDFRAMEBUFFERPROC, bind_framebuffer);
    FUNCTION(PFNGLFRAMEBUFFERTEXTURE2DPROC, framebuffer_texture);
    FUNCTION(PFNGLCHECKFRAMEBUFFERSTATUSPROC, framebuffer_status);
    FUNCTION(PFNGLDELETEFRAMEBUFFERSPROC, delete_framebuffers);
    FUNCTION(PFNGLACTIVETEXTUREPROC, active_texture);
    FUNCTION(decltype(&glGetIntegerv), get_integer);
    FUNCTION(decltype(&glGetBooleanv), get_boolean);
    FUNCTION(decltype(&glIsEnabled), is_enabled);
    FUNCTION(decltype(&glEnable), enable);
    FUNCTION(decltype(&glDisable), disable);
    FUNCTION(decltype(&glColorMask), color_mask);
    FUNCTION(decltype(&glViewport), viewport);
    FUNCTION(decltype(&glGenTextures), gen_textures);
    FUNCTION(decltype(&glBindTexture), bind_texture);
    FUNCTION(decltype(&glTexImage2D), texture_image);
    FUNCTION(decltype(&glTexParameteri), texture_parameter);
    FUNCTION(decltype(&glDeleteTextures), delete_textures);
    FUNCTION(decltype(&glDrawArrays), draw_arrays);
#undef FUNCTION
    SDL_Renderer* renderer;
    SDL_Window* window;
    SDL_GLContext context;
    bool es;
    GLuint program = 0, buffer = 0, framebuffer = 0, output = 0;
    GLint frame_uniform = -1, adjustment_uniform = -1, gain_uniform = -1, source_uniform = -1;
    SDL_Texture* texture = nullptr;
    unsigned width = 0, height = 0;

    template <typename T> void load(T& function, const char* name, const char* fallback = nullptr)
    {
        function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
        if (!function && fallback)
        {
            function = reinterpret_cast<T>(SDL_GL_GetProcAddress(fallback));
        }
        if (!function)
        {
            throw std::runtime_error(std::string("Missing shader function: ") + name);
        }
    }
    explicit GlPass(SDL_Renderer* renderer)
        : renderer(renderer), window(SDL_GL_GetCurrentWindow()), context(SDL_GL_GetCurrentContext()),
          es(std::string_view(SDL_GetRendererName(renderer)) == "opengles2")
    {
#define LOAD(member, function) load(member, "gl" #function)
        LOAD(create_shader, CreateShader);
        LOAD(shader_source, ShaderSource);
        LOAD(compile_shader, CompileShader);
        LOAD(get_shader_iv, GetShaderiv);
        LOAD(get_shader_log, GetShaderInfoLog);
        LOAD(delete_shader, DeleteShader);
        LOAD(create_program, CreateProgram);
        LOAD(attach_shader, AttachShader);
        LOAD(bind_attribute, BindAttribLocation);
        LOAD(link_program, LinkProgram);
        LOAD(get_program_iv, GetProgramiv);
        LOAD(get_program_log, GetProgramInfoLog);
        LOAD(delete_program, DeleteProgram);
        LOAD(use_program, UseProgram);
        LOAD(uniform_location, GetUniformLocation);
        LOAD(uniform_4fv, Uniform4fv);
        LOAD(uniform_1i, Uniform1i);
        LOAD(gen_buffers, GenBuffers);
        LOAD(bind_buffer, BindBuffer);
        LOAD(buffer_data, BufferData);
        LOAD(delete_buffers, DeleteBuffers);
        LOAD(attribute_pointer, VertexAttribPointer);
        LOAD(enable_attribute, EnableVertexAttribArray);
        LOAD(disable_attribute, DisableVertexAttribArray);
        LOAD(get_attribute, GetVertexAttribiv);
        LOAD(get_attribute_pointer, GetVertexAttribPointerv);
        LOAD(active_texture, ActiveTexture);
        LOAD(get_integer, GetIntegerv);
        LOAD(get_boolean, GetBooleanv);
        LOAD(is_enabled, IsEnabled);
        LOAD(enable, Enable);
        LOAD(disable, Disable);
        LOAD(color_mask, ColorMask);
        LOAD(viewport, Viewport);
        LOAD(gen_textures, GenTextures);
        LOAD(bind_texture, BindTexture);
        LOAD(texture_image, TexImage2D);
        LOAD(texture_parameter, TexParameteri);
        LOAD(delete_textures, DeleteTextures);
        LOAD(draw_arrays, DrawArrays);
#undef LOAD
        load(gen_framebuffers, "glGenFramebuffers", "glGenFramebuffersEXT");
        load(bind_framebuffer, "glBindFramebuffer", "glBindFramebufferEXT");
        load(framebuffer_texture, "glFramebufferTexture2D", "glFramebufferTexture2DEXT");
        load(framebuffer_status, "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
        load(delete_framebuffers, "glDeleteFramebuffers", "glDeleteFramebuffersEXT");
    }
    ~GlPass() override
    {
        auto* previous_window = SDL_GL_GetCurrentWindow();
        const auto previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window, context))
        {
            return;
        }
        SDL_FlushRenderer(renderer);
        SDL_DestroyTexture(texture);
        if (output)
        {
            delete_textures(1, &output);
        }
        if (framebuffer)
        {
            delete_framebuffers(1, &framebuffer);
        }
        if (buffer)
        {
            delete_buffers(1, &buffer);
        }
        if (program)
        {
            delete_program(program);
        }
        if (previous_context != context)
        {
            SDL_GL_MakeCurrent(previous_window, previous_context);
        }
    }
    GLuint compile(GLenum stage, const char* body)
    {
        const std::string header = es ? "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp "
                                        "float;\n#else\nprecision mediump float;\n#endif\n"
                                      : "#version 120\n";
        const char* source[]{header.c_str(), body};
        GLuint shader = create_shader(stage);
        shader_source(shader, 2, source, nullptr);
        compile_shader(shader);
        GLint status = 0;
        get_shader_iv(shader, GL_COMPILE_STATUS, &status);
        if (!status)
        {
            std::array<char, 2048> log{};
            get_shader_log(shader, log.size(), nullptr, log.data());
            delete_shader(shader);
            throw std::runtime_error(std::string("Color correction shader: ") + log.data());
        }
        return shader;
    }
    void initialize()
    {
        if (program)
        {
            return;
        }
        const auto vertex = compile(GL_VERTEX_SHADER, color_gl_vertex);
        GLuint fragment = 0;
        try
        {
            fragment = compile(GL_FRAGMENT_SHADER, color_gl_fragment);
        }
        catch (...)
        {
            delete_shader(vertex);
            throw;
        }
        program = create_program();
        attach_shader(program, vertex);
        attach_shader(program, fragment);
        bind_attribute(program, 0, "position");
        link_program(program);
        delete_shader(vertex);
        delete_shader(fragment);
        GLint linked = 0;
        get_program_iv(program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            std::array<char, 2048> log{};
            get_program_log(program, log.size(), nullptr, log.data());
            throw std::runtime_error(std::string("Color correction program: ") + log.data());
        }
        gen_buffers(1, &buffer);
        gen_framebuffers(1, &framebuffer);
        frame_uniform = uniform_location(program, "frame");
        adjustment_uniform = uniform_location(program, "adjustment");
        gain_uniform = uniform_location(program, "gain");
        source_uniform = uniform_location(program, "source");
        GLint previous_buffer;
        get_integer(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
        constexpr std::array<float, 6> vertices{-1, -1, 3, -1, -1, 3};
        bind_buffer(GL_ARRAY_BUFFER, buffer);
        buffer_data(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
        bind_buffer(GL_ARRAY_BUFFER, previous_buffer);
    }
    SDL_Texture* draw(SDL_Texture* input, unsigned frame_width, unsigned frame_height,
                      const shaders::Shader& settings) override
    {
        if (!SDL_FlushRenderer(renderer))
        {
            throw std::runtime_error(SDL_GetError());
        }
        initialize();
        GLint old_program, old_buffer, old_framebuffer, old_active, old_texture;
        std::array<GLint, 4> old_viewport{};
        std::array<GLboolean, 4> mask{};
        get_integer(GL_CURRENT_PROGRAM, &old_program);
        get_integer(GL_ARRAY_BUFFER_BINDING, &old_buffer);
        get_integer(GL_FRAMEBUFFER_BINDING, &old_framebuffer);
        get_integer(GL_ACTIVE_TEXTURE, &old_active);
        active_texture(GL_TEXTURE0);
        get_integer(GL_TEXTURE_BINDING_2D, &old_texture);
        get_integer(GL_VIEWPORT, old_viewport.data());
        get_boolean(GL_COLOR_WRITEMASK, mask.data());
        constexpr std::array<GLenum, 5> capabilities{GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
                                                     GL_CULL_FACE};
        std::array<GLboolean, capabilities.size()> enabled{};
        for (std::size_t i = 0; i < capabilities.size(); ++i)
        {
            enabled[i] = is_enabled(capabilities[i]);
            disable(capabilities[i]);
        }
        GLint attribute_enabled, attribute_size, attribute_type, attribute_normalized, attribute_stride,
            attribute_buffer;
        void* attribute_offset = nullptr;
        get_attribute(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribute_enabled);
        get_attribute(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attribute_size);
        get_attribute(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attribute_type);
        get_attribute(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attribute_normalized);
        get_attribute(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attribute_stride);
        get_attribute(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attribute_buffer);
        get_attribute_pointer(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attribute_offset);
        auto restore = [&]
        {
            bind_buffer(GL_ARRAY_BUFFER, attribute_buffer);
            attribute_pointer(0, attribute_size, attribute_type, attribute_normalized, attribute_stride,
                              attribute_offset);
            if (!attribute_enabled)
            {
                disable_attribute(0);
            }
            bind_buffer(GL_ARRAY_BUFFER, old_buffer);
            use_program(old_program);
            bind_texture(GL_TEXTURE_2D, old_texture);
            active_texture(old_active);
            bind_framebuffer(GL_FRAMEBUFFER, old_framebuffer);
            viewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
            color_mask(mask[0], mask[1], mask[2], mask[3]);
            for (std::size_t i = 0; i < capabilities.size(); ++i)
            {
                if (enabled[i])
                {
                    enable(capabilities[i]);
                }
            }
        };
        try
        {
            if (!texture || width != frame_width || height != frame_height)
            {
                SDL_DestroyTexture(texture);
                texture = nullptr;
                if (output)
                {
                    delete_textures(1, &output);
                }
                gen_textures(1, &output);
                bind_texture(GL_TEXTURE_2D, output);
                texture_image(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                              nullptr);
                texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                const auto props = SDL_CreateProperties();
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame_width);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame_height);
                SDL_SetNumberProperty(props,
                                      es ? SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER
                                         : SDL_PROP_TEXTURE_CREATE_OPENGL_TEXTURE_NUMBER,
                                      output);
                texture = SDL_CreateTextureWithProperties(renderer, props);
                SDL_DestroyProperties(props);
                if (!texture)
                {
                    throw std::runtime_error(SDL_GetError());
                }
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
                width = frame_width;
                height = frame_height;
            }
            bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
            framebuffer_texture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
            if (framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                throw std::runtime_error("Incomplete shader framebuffer");
            }
            viewport(0, 0, width, height);
            color_mask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            const auto input_props = SDL_GetTextureProperties(input);
            const auto native = SDL_GetNumberProperty(
                input_props, es ? SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER : SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER,
                0);
            bind_texture(GL_TEXTURE_2D, static_cast<GLuint>(native));
            use_program(program);
            std::array<float, 4> adjustment{}, gain{1, 1, 1, 0};
            for (int i = 0; i < 4; ++i)
            {
                adjustment[i] = settings.parameters[i] / 100.f;
            }
            for (int i = 0; i < 3; ++i)
            {
                gain[i] = settings.parameters[i + 4] / 100.f;
            }
            float input_width, input_height;
            SDL_GetTextureSize(input, &input_width, &input_height);
            const std::array<float, 4> source{width / input_width, height / input_height, 0, 0};
            uniform_1i(frame_uniform, 0);
            uniform_4fv(adjustment_uniform, 1, adjustment.data());
            uniform_4fv(gain_uniform, 1, gain.data());
            uniform_4fv(source_uniform, 1, source.data());
            bind_buffer(GL_ARRAY_BUFFER, buffer);
            attribute_pointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            enable_attribute(0);
            draw_arrays(GL_TRIANGLES, 0, 3);
            restore();
        }
        catch (...)
        {
            restore();
            throw;
        }
        return texture;
    }
};
} // namespace

std::unique_ptr<ShaderPass> create_gl_shader_pass(SDL_Renderer* renderer)
{
    if (!SDL_FlushRenderer(renderer))
    {
        throw std::runtime_error(SDL_GetError());
    }
    return std::make_unique<GlPass>(renderer);
}
