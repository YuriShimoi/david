#pragma once
#include "core/Effect.h"
#include <string>

namespace david {

// Runs a shader mod's fragment code over an image on the GPU (needs a current GL 3.3 context).
// The mod defines vec4 effect(vec2 pos); pos and pixel(pos) use image coordinates.
// Programs are cached by source; a compile error comes back in error.
bool runShader(const std::string& source, const std::vector<EffectParam>& params, const Image& src, Image& dst,
               const ParamValues& v, const EffectEnv& env, std::string& error);
// Compiles only, to report errors when a mod loads
bool checkShader(const std::string& source, const std::vector<EffectParam>& params, std::string& error);
void releaseShaders();

}
