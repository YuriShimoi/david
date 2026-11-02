#include "render/ShaderRunner.h"
#include <glad/glad.h>
#include <unordered_map>

namespace david {

namespace {
const char* VS = R"(#version 330 core
const vec2 P[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
void main() { gl_Position = vec4(P[gl_VertexID], 0.0, 1.0); }
)";

// The mod's code sits between this header and main(); rows are stored top first, so no flip is needed
std::string fragment(const std::string& source, const std::vector<EffectParam>& params) {
  std::string s = "#version 330 core\nuniform sampler2D uImage;\nuniform vec2 uSize;\nuniform vec2 uOrigin;\nout vec4 oColor;\n"
                  "vec4 pixel(vec2 pos) { return texture(uImage, (floor(pos - uOrigin) + 0.5) / uSize); }\n";
  for(auto& p : params) {
    const char* type = p.type == EParam::Float ? "float" : p.type == EParam::Color ? "vec4" : p.type == EParam::Bool ? "bool" : "int";
    if(p.type == EParam::Text || p.type == EParam::Curve) continue;
    s += std::string("uniform ") + type + " " + p.id + ";\n";
  }
  s += "#line 1\n" + source + "\nvoid main() { oColor = clamp(effect(floor(gl_FragCoord.xy) + uOrigin), 0.0, 1.0); }\n";
  return s;
}

struct Program { GLuint id = 0; std::string error; };
std::unordered_map<std::string, Program> s_cache;
GLuint s_vao = 0;

Program& program(const std::string& source, const std::vector<EffectParam>& params) {
  std::string fs = fragment(source, params);
  auto it = s_cache.find(fs);
  if(it != s_cache.end()) return it->second;
  Program& p = s_cache[fs];
  auto compile = [&](GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if(!ok) {
      char log[2048];
      glGetShaderInfoLog(sh, sizeof log, nullptr, log);
      p.error += log;
    }
    return sh;
  };
  GLuint v = compile(GL_VERTEX_SHADER, VS), f = compile(GL_FRAGMENT_SHADER, fs.c_str());
  if(p.error.empty()) {
    p.id = glCreateProgram();
    glAttachShader(p.id, v);
    glAttachShader(p.id, f);
    glLinkProgram(p.id);
    GLint ok = 0;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if(!ok) { char log[2048]; glGetProgramInfoLog(p.id, sizeof log, nullptr, log); p.error = log; glDeleteProgram(p.id); p.id = 0; }
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
}
}

bool checkShader(const std::string& source, const std::vector<EffectParam>& params, std::string& error) {
  Program& p = program(source, params);
  error = p.error;
  return p.id != 0;
}

bool runShader(const std::string& source, const std::vector<EffectParam>& params, const Image& src, Image& dst,
               const ParamValues& v, const EffectEnv& env, std::string& error) {
  Program& p = program(source, params);
  if(!p.id) { error = p.error; return false; }
  if(!s_vao) glGenVertexArrays(1, &s_vao);
  GLint prevFbo = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);

  GLuint tex[2], fbo;
  glGenTextures(2, tex);
  for(int i = 0; i < 2; i++) {
    glBindTexture(GL_TEXTURE_2D, tex[i]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src.w, src.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, i == 0 ? src.px.data() : nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1], 0);
  glViewport(0, 0, src.w, src.h);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glUseProgram(p.id);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex[0]);
  glBindSampler(0, 0);
  glUniform1i(glGetUniformLocation(p.id, "uImage"), 0);
  glUniform2f(glGetUniformLocation(p.id, "uSize"), float(src.w), float(src.h));
  glUniform2f(glGetUniformLocation(p.id, "uOrigin"), float(env.originX), float(env.originY));
  for(auto& prm : params) {
    GLint loc = glGetUniformLocation(p.id, prm.id.c_str());
    if(loc < 0) continue;
    double val = v.get(prm.id, prm.def);
    if(prm.type == EParam::Float) glUniform1f(loc, float(val));
    else if(prm.type == EParam::Color) {
      uint32_t c = uint32_t(val);
      glUniform4f(loc, (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f);
    } else if(prm.type != EParam::Text && prm.type != EParam::Curve) glUniform1i(loc, int(val));
  }
  glBindVertexArray(s_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, src.w, src.h, GL_RGBA, GL_UNSIGNED_BYTE, dst.px.data());
  glUseProgram(0);

  glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  if(blend) glEnable(GL_BLEND);
  if(scissor) glEnable(GL_SCISSOR_TEST);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(2, tex);
  return true;
}

void releaseShaders() {
  for(auto& [k, p] : s_cache) if(p.id) glDeleteProgram(p.id);
  s_cache.clear();
  if(s_vao) glDeleteVertexArrays(1, &s_vao);
  s_vao = 0;
}

}
