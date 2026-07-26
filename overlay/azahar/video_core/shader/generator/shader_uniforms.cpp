// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/pica/shader_setup.h"
#include "video_core/shader/generator/shader_uniforms.h"

// Hoenn Forge overlay of upstream src/video_core/shader/generator/shader_uniforms.cpp.
// Upstream base: azahar c711b0ab324d2ec116292b0db7c04a5733b8e559.
// The only change is the ApplyToUniforms() call at the end of SetFromRegs(); everything
// else is upstream verbatim. SetFromRegs is shared by RasterizerOpenGL::UploadUniforms
// and RasterizerVulkan::UploadUniforms, so hooking it here covers both backends — and in
// particular the Thor's Vulkan path — from one place.
#include "video_core/hoenn_gpu_cam.h"

namespace Pica::Shader::Generator {

void VSPicaUniformData::SetFromRegs(const Pica::ShaderSetup& setup) {
    b = 0;
    for (u32 j = 0; j < setup.uniforms.b.size(); j++) {
        b |= setup.uniforms.b[j] << j;
    }
    for (u32 j = 0; j < setup.uniforms.i.size(); j++) {
        const auto& value = setup.uniforms.i[j];
        i[j] = Common::MakeVec<u32>(value.x, value.y, value.z, value.w);
    }
    for (u32 j = 0; j < setup.uniforms.f.size(); j++) {
        const auto& value = setup.uniforms.f[j];
        f[j] = Common::MakeVec<f32>(value.x.ToFloat32(), value.y.ToFloat32(), value.z.ToFloat32(),
                                    value.w.ToFloat32());
    }

    // Hoenn Forge: rotate the view transform for outdoor free look. Operates on our copy
    // of the uniform block only — guest memory and Pica::ShaderSetup are untouched.
    Hoenn::GpuCam::ApplyToUniforms(f);
}

} // namespace Pica::Shader::Generator
