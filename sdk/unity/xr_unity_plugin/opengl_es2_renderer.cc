/*
 * Copyright 2020 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifdef __ANDROID__
#include <GLES2/gl2.h>
#endif
#ifdef __APPLE__
#include <OpenGLES/ES2/gl.h>
#endif
#include "util/logging.h"
#include "unity/xr_unity_plugin/renderer.h"

// @def Forwards the call to CheckGlError().
#define CHECKGLERROR(label) CheckGlError(__FILE__, __LINE__, label)

namespace cardboard::unity {
namespace {

/**
 * Checks for OpenGL errors, and crashes if one has occurred.  Note that this
 * can be an expensive call, so real applications should call this rarely.
 *
 * @param file File name
 * @param line Line number
 * @param label Error label
 */
void CheckGlError(const char* file, int line, const char* label) {
  const int gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    CARDBOARD_LOGF("[%s : %d] GL error: %d. %s", file, line, gl_error, label);
    // Crash immediately to make OpenGL errors obvious.
    abort();
  }
}

// TODO(b/155457703): De-dupe GL utility function here and in
// distortion_renderer.cc
GLuint LoadShader(GLenum shader_type, const char* source) {
  GLuint shader = glCreateShader(shader_type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  CHECKGLERROR("glCompileShader");
  GLint result = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
  if (result == GL_FALSE) {
    int log_length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length == 0) {
      return 0;
    }

    std::vector<char> log_string(log_length);
    glGetShaderInfoLog(shader, log_length, nullptr, log_string.data());
    CARDBOARD_LOGE("Could not compile shader of type %d: %s", shader_type,
                   log_string.data());

    shader = 0;
  }

  return shader;
}

// TODO(b/155457703): De-dupe GL utility function here and in
// distortion_renderer.cc
GLuint CreateProgram(const char* vertex, const char* fragment) {
  GLuint vertex_shader = LoadShader(GL_VERTEX_SHADER, vertex);
  if (vertex_shader == 0) {
    return 0;
  }

  GLuint fragment_shader = LoadShader(GL_FRAGMENT_SHADER, fragment);
  if (fragment_shader == 0) {
    return 0;
  }

  GLuint program = glCreateProgram();

  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  CHECKGLERROR("glLinkProgram");

  GLint result = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &result);
  if (result == GL_FALSE) {
    int log_length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length == 0) {
      return 0;
    }

    std::vector<char> log_string(log_length);
    glGetShaderInfoLog(program, log_length, nullptr, log_string.data());
    CARDBOARD_LOGE("Could not compile program: %s", log_string.data());

    return 0;
  }

  glDetachShader(program, vertex_shader);
  glDetachShader(program, fragment_shader);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  CHECKGLERROR("GlCreateProgram");

  return program;
}

/// @brief Vertex shader for rendering the widgets when using OpenGL ES2.0.
const char kWidgetVertexShaderOpenGlEs2[] =
    R"glsl(
  attribute vec2 a_Position;
  attribute vec2 a_TexCoords;
  varying vec2 v_TexCoords;
  void main() {
    gl_Position = vec4(a_Position, 0, 1);
    v_TexCoords = a_TexCoords;
  }
  )glsl";

/// @brief Fragment shader for rendering the widgets when using OpenGL ES2.0.
const char kWidgetFragmentShaderOpenGlEs2[] =
    R"glsl(
  precision mediump float;
  uniform sampler2D u_Texture;
  varying vec2 v_TexCoords;
  void main() {
    gl_FragColor = texture2D(u_Texture, v_TexCoords);
  }
  )glsl";

class OpenGlEs2Renderer : public Renderer {
 public:
  OpenGlEs2Renderer() = default;
  ~OpenGlEs2Renderer() { TeardownWidgets(); }

  void SetupWidgets() override {
    if (widget_program_ != 0) {
      return;
    }

    widget_program_ = CreateProgram(kWidgetVertexShaderOpenGlEs2,
                                    kWidgetFragmentShaderOpenGlEs2);
    widget_attrib_position_ =
        glGetAttribLocation(widget_program_, "a_Position");
    widget_attrib_tex_coords_ =
        glGetAttribLocation(widget_program_, "a_TexCoords");
    widget_uniform_texture_ =
        glGetUniformLocation(widget_program_, "u_Texture");
  }

  void RenderWidgets(const ScreenParams& screen_params,
                     const std::vector<WidgetParams>& widget_params) override {
    if (widget_program_ == 0) {
      CARDBOARD_LOGF(
          "Trying to RenderWidgets without setting up the renderer.");
      return;
    }

    glViewport(screen_params.viewport_x, screen_params.viewport_y,
               screen_params.viewport_width, screen_params.viewport_height);

    for (const WidgetParams& widget_param : widget_params) {
      RenderWidget(screen_params.viewport_width, screen_params.viewport_height,
                   widget_param);
    }
  }

  void TeardownWidgets() override {
    if (widget_program_ == 0) {
      return;
    }
    glDeleteProgram(widget_program_);
    CHECKGLERROR("GlDeleteProgram");
    widget_program_ = 0;
  }

  void CreateRenderTexture(RenderTexture* render_texture, int screen_width,
                           int screen_height) override {
    // Create texture color buffer.
    GLuint tmp = 0;
    glGenTextures(1, &tmp);
    glBindTexture(GL_TEXTURE_2D, tmp);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screen_width / 2, screen_height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, 0);
    CHECKGLERROR("Create texture color buffer.");
    render_texture->color_buffer = tmp;

    // Create texture depth buffer.
    tmp = 0;
    glGenRenderbuffers(1, &tmp);
    glBindRenderbuffer(GL_RENDERBUFFER, tmp);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
                          screen_width / 2, screen_height);
    CHECKGLERROR("Create texture depth buffer.");
    render_texture->depth_buffer = tmp;
  }

  void DestroyRenderTexture(RenderTexture* render_texture) override {
    GLuint tmp = static_cast<GLuint>(render_texture->depth_buffer);
    glDeleteRenderbuffers(1, &tmp);
    render_texture->depth_buffer = 0;

    tmp = static_cast<GLuint>(render_texture->color_buffer);
    glDeleteTextures(1, &tmp);
    render_texture->color_buffer = 0;
  }

  void RenderEyesToDisplay(
      CardboardDistortionRenderer* renderer, const ScreenParams& screen_params,
      const CardboardEyeTextureDescription* left_eye,
      const CardboardEyeTextureDescription* right_eye) override {
    int bound_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound_framebuffer);
    CardboardDistortionRenderer_renderEyeToDisplay(
        renderer, bound_framebuffer, screen_params.viewport_x,
        screen_params.viewport_y, screen_params.viewport_width,
        screen_params.viewport_height, left_eye, right_eye);
  }

 private:
  static constexpr float Lerp(float start, float end, float val) {
    return start + (end - start) * val;
  }

  void RenderWidget(int screen_width, int screen_height,
                    const WidgetParams& params) {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    // Convert coordinates to normalized space (-1,-1 - +1,+1)
    float x = Lerp(-1, +1, static_cast<float>(params.x) / screen_width);
    float y = Lerp(-1, +1, static_cast<float>(params.y) / screen_height);
    float width = params.width * 2.0f / screen_width;
    float height = params.height * 2.0f / screen_height;
    const float position[] = {x, y,          x + width, y,
                              x, y + height, x + width, y + height};

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(widget_attrib_position_);
    glVertexAttribPointer(
        widget_attrib_position_, /*size=*/2, /*type=*/GL_FLOAT,
        /*normalized=*/GL_FALSE, /*stride=*/0, /*pointer=*/position);
    CHECKGLERROR("RenderWidget");

    const float uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glEnableVertexAttribArray(widget_attrib_tex_coords_);
    glVertexAttribPointer(
        widget_attrib_tex_coords_, /*size=*/2, /*type=*/GL_FLOAT,
        /*normalized=*/GL_FALSE, /*stride=*/0, /*pointer=*/uv);

    glUseProgram(widget_program_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<int>(params.texture));
    glUniform1i(widget_uniform_texture_, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    CHECKGLERROR("RenderWidget");
  }

  // @brief Widgets GL program.
  GLuint widget_program_{0};

  // @brief Widgets "a_Position" attrib location.
  GLint widget_attrib_position_;

  // @brief Widgets "a_TexCoords" attrib location.
  GLint widget_attrib_tex_coords_;

  // @brief Widgets "u_Texture" uniform location.
  GLint widget_uniform_texture_;
};

}  // namespace

std::unique_ptr<Renderer> MakeOpenGlEs2Renderer() {
  return std::make_unique<OpenGlEs2Renderer>();
}

}  // namespace cardboard::unity