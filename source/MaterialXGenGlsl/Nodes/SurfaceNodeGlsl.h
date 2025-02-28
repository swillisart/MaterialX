//
// TM & (c) 2017 Lucasfilm Entertainment Company Ltd. and Lucasfilm Ltd.
// All rights reserved.  See LICENSE.txt for license.
//

#ifndef MATERIALX_SURFACENODEGLSL_H
#define MATERIALX_SURFACENODEGLSL_H

#include <MaterialXGenGlsl/Export.h>
#include <MaterialXGenGlsl/GlslShaderGenerator.h>

namespace MaterialX
{

/// Surface node implementation for GLSL
class MX_GENGLSL_API SurfaceNodeGlsl : public GlslImplementation
{
  public:
    SurfaceNodeGlsl();

    static ShaderNodeImplPtr create();

    void createVariables(const ShaderNode& node, GenContext& context, Shader& shader) const override;

    void emitFunctionCall(const ShaderNode& node, GenContext& context, ShaderStage& stage) const override;

    virtual void emitLightLoop(const ShaderNode& node, GenContext& context, ShaderStage& stage, const string& outColor) const;

  protected:
    /// Closure contexts for calling closure functions.
    HwClosureContextPtr _callReflection;
    HwClosureContextPtr _callTransmission;
    HwClosureContextPtr _callIndirect;
    HwClosureContextPtr _callEmission;
};

} // namespace MaterialX

#endif
