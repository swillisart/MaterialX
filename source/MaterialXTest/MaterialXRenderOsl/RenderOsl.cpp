//
// TM & (c) 2017 Lucasfilm Entertainment Company Ltd. and Lucasfilm Ltd.
// All rights reserved.  See LICENSE.txt for license.
//

#include <MaterialXTest/Catch/catch.hpp>
#include <MaterialXTest/MaterialXRender/RenderUtil.h>

#include <MaterialXGenShader/TypeDesc.h>
#include <MaterialXGenShader/Util.h>

#include <MaterialXGenOsl/OslShaderGenerator.h>

#include <MaterialXRenderOsl/OslRenderer.h>

#include <MaterialXRender/StbImageLoader.h>
#if defined(MATERIALX_BUILD_OIIO)
#include <MaterialXRender/OiioImageLoader.h>
#endif

namespace mx = MaterialX;

class OslShaderRenderTester : public RenderUtil::ShaderRenderTester
{
  public:
    explicit OslShaderRenderTester(mx::ShaderGeneratorPtr shaderGenerator) :
        RenderUtil::ShaderRenderTester(shaderGenerator)
    {
    }

  protected:
    void registerSourceCodeSearchPaths(mx::GenContext& context) override
    {
        // Include extra OSL implementation files
        mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("libraries");
        context.registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        // Include current path to find resources.
        context.registerSourceCodeSearchPath(mx::FilePath::getCurrentPath());
    }

    void createRenderer(std::ostream& log) override;

    bool runRenderer(const std::string& shaderName,
                     mx::TypedElementPtr element,
                     mx::GenContext& context,
                     mx::DocumentPtr doc,
                     std::ostream& log,
                     const GenShaderUtil::TestSuiteOptions& testOptions,
                     RenderUtil::RenderProfileTimes& profileTimes,
                     const mx::FileSearchPath& imageSearchPath,
                     const std::string& outputPath = ".",
                     mx::ImageVec* imageVec = nullptr) override;

    bool saveImage(const mx::FilePath& filePath, mx::ConstImagePtr image, bool /*verticalFlip*/) const override
    {
        return _renderer->getImageHandler()->saveImage(filePath, image, false);
    }

    mx::ImageLoaderPtr _imageLoader;
    mx::OslRendererPtr _renderer;
};

// Renderer setup
void OslShaderRenderTester::createRenderer(std::ostream& log)
{
    bool initialized = false;

    _renderer = mx::OslRenderer::create();
    _imageLoader = mx::StbImageLoader::create();

    // Set up additional utilities required to run OSL testing including
    // oslc and testrender paths and OSL include path
    //
    const std::string oslcExecutable(MATERIALX_TESTOSLC_EXECUTABLE);
    _renderer->setOslCompilerExecutable(oslcExecutable);
    const std::string testRenderExecutable(MATERIALX_TESTRENDER_EXECUTABLE);
    _renderer->setOslTestRenderExecutable(testRenderExecutable);
    _renderer->setOslIncludePath(mx::FilePath(MATERIALX_OSL_INCLUDE_PATH));

    try
    {
        _renderer->initialize();

        mx::StbImageLoaderPtr stbLoader = mx::StbImageLoader::create();
        mx::ImageHandlerPtr imageHandler = mx::ImageHandler::create(stbLoader);
#if defined(MATERIALX_BUILD_OIIO)
        mx::OiioImageLoaderPtr oiioLoader = mx::OiioImageLoader::create();
        imageHandler->addLoader(oiioLoader);
#endif
        _renderer->setImageHandler(imageHandler);
        _renderer->setLightHandler(nullptr);
        initialized = true;

        // Pre-compile some required shaders for testrender
        if (!oslcExecutable.empty() && !testRenderExecutable.empty())
        {
            mx::FilePath shaderPath = mx::FilePath::getCurrentPath() / mx::FilePath("resources/Materials/TestSuite/Utilities/");
            _renderer->setOslOutputFilePath(shaderPath);

            const std::string OSL_EXTENSION("osl");
            for (const mx::FilePath& filename : shaderPath.getFilesInDirectory(OSL_EXTENSION))
            {
                _renderer->compileOSL(shaderPath / filename);
            }

            // Set the search path for these compiled shaders.
            _renderer->setOslUtilityOSOPath(shaderPath);
        }
    }
    catch (mx::ExceptionRenderError& e)
    {
        for (const auto& error : e.errorLog())
        {
            log << e.what() << " " << error << std::endl;
        }
    }
    REQUIRE(initialized);
}

// Renderer execution
bool OslShaderRenderTester::runRenderer(const std::string& shaderName,
                                         mx::TypedElementPtr element,
                                         mx::GenContext& context,
                                         mx::DocumentPtr doc,
                                         std::ostream& log,
                                         const GenShaderUtil::TestSuiteOptions& testOptions,
                                         RenderUtil::RenderProfileTimes& profileTimes,
                                         const mx::FileSearchPath& imageSearchPath,
                                         const std::string& outputPath,
                                         mx::ImageVec* imageVec)
{
    RenderUtil::AdditiveScopedTimer totalOSLTime(profileTimes.languageTimes.totalTime, "OSL total time");

    const mx::ShaderGenerator& shadergen = context.getShaderGenerator();

    // Perform validation if requested
    if (testOptions.validateElementToRender)
    {
        std::string message;
        if (!element->validate(&message))
        {
            log << "Element is invalid: " << message << std::endl;
            return false;
        }
    }

    std::vector<mx::GenOptions> optionsList;
    getGenerationOptions(testOptions, context.getOptions(), optionsList);

    if (element && doc)
    {
        log << "------------ Run OSL validation with element: " << element->getNamePath() << "-------------------" << std::endl;

        for (const auto& options : optionsList)
        {
            profileTimes.elementsTested++;

            mx::ShaderPtr shader;
            try
            {
                RenderUtil::AdditiveScopedTimer genTimer(profileTimes.languageTimes.generationTime, "OSL generation time");
                mx::GenOptions& contextOptions = context.getOptions();
                contextOptions = options;
                contextOptions.targetColorSpaceOverride = "lin_rec709";
                shader = shadergen.generate(shaderName, element, context);
            }
            catch (mx::Exception& e)
            {
                log << ">> " << e.what() << "\n";
                shader = nullptr;
            }
            CHECK(shader != nullptr);
            if (shader == nullptr)
            {
                log << ">> Failed to generate shader\n";
                return false;
            }
            CHECK(shader->getSourceCode().length() > 0);

            std::string shaderPath;
            mx::FilePath outputFilePath = outputPath;
            // Use separate directory for reduced output
            if (options.shaderInterfaceType == mx::SHADER_INTERFACE_REDUCED)
            {
                outputFilePath = outputFilePath / mx::FilePath("reduced");
            }

            // Note: mkdir will fail if the directory already exists which is ok.
            {
                RenderUtil::AdditiveScopedTimer ioDir(profileTimes.languageTimes.ioTime, "OSL dir time");
                outputFilePath.createDirectory();
            }

            shaderPath = mx::FilePath(outputFilePath) / mx::FilePath(shaderName);

            // Write out osl file
            if (testOptions.dumpGeneratedCode)
            {
                RenderUtil::AdditiveScopedTimer ioTimer(profileTimes.languageTimes.ioTime, "OSL I/O time");
                std::ofstream file;
                file.open(shaderPath + ".osl");
                file << shader->getSourceCode();
                file.close();
            }

            if (!testOptions.compileCode)
            {
                return false;
            }

            // Validate
            bool validated = false;
            try
            {
                // Set output path and shader name
                _renderer->setOslOutputFilePath(outputFilePath);
                _renderer->setOslShaderName(shaderName);

                // Validate compilation
                {
                    RenderUtil::AdditiveScopedTimer compileTimer(profileTimes.languageTimes.compileTime, "OSL compile time");
                    _renderer->createProgram(shader);
                }

                if (testOptions.renderImages)
                {
                    _renderer->setSize(static_cast<unsigned int>(testOptions.renderSize[0]), static_cast<unsigned int>(testOptions.renderSize[1]));

                    const mx::ShaderStage& stage = shader->getStage(mx::Stage::PIXEL);

                    // Look for textures and build parameter override string for each image
                    // files if a relative path maps to an absolute path
                    const mx::VariableBlock& uniforms = stage.getUniformBlock(mx::OSL::UNIFORMS);

                    mx::StringVec overrides;
                    mx::StringVec envOverrides;
                    mx::StringMap separatorMapper;
                    separatorMapper["\\\\"] = "/";
                    separatorMapper["\\"] = "/";
                    for (size_t i = 0; i<uniforms.size(); ++i)
                    {
                        const mx::ShaderPort* uniform = uniforms[i];

                        // Bind input images
                        if (uniform->getType() != MaterialX::Type::FILENAME)
                        {
                            continue;
                        }
                        if (uniform->getValue())
                        {
                            const std::string& uniformName = uniform->getName();
                            mx::FilePath filename;
                            mx::FilePath origFilename(uniform->getValue()->getValueString());
                            if (!origFilename.isAbsolute())
                            {
                                filename = imageSearchPath.find(origFilename);
                                if (filename != origFilename)
                                {
                                    std::string overrideString("string " + uniformName + " \"" + filename.asString() + "\";\n");
                                    overrideString = mx::replaceSubstrings(overrideString, separatorMapper);
                                    overrides.push_back(overrideString);
                                }
                            }
                        }
                    }
                    // Bind IBL image name overrides.
                    std::string envmap_filename("string envmap_filename \"");
                    envmap_filename += testOptions.radianceIBLPath;
                    envmap_filename += "\";\n";                    
                    envOverrides.push_back(envmap_filename);

                    _renderer->setShaderParameterOverrides(overrides);
                    _renderer->setEnvShaderParameterOverrides(envOverrides);

                    const mx::VariableBlock& outputs = stage.getOutputBlock(mx::OSL::OUTPUTS);
                    if (outputs.size() > 0)
                    {
                        const mx::ShaderPort* output = outputs[0];
                        const mx::TypeSyntax& typeSyntax = shadergen.getSyntax().getTypeSyntax(output->getType());

                        const std::string& outputName = output->getVariable();
                        const std::string& outputType = typeSyntax.getTypeAlias().empty() ? typeSyntax.getName() : typeSyntax.getTypeAlias();

                        static const std::string SHADING_SCENE_FILE = "closure_color_scene.xml";
                        static const std::string NON_SHADING_SCENE_FILE = "constant_color_scene.xml";
                        const std::string& sceneTemplateFile = mx::elementRequiresShading(element) ? SHADING_SCENE_FILE : NON_SHADING_SCENE_FILE;

                        // Set shader output name and type to use
                        _renderer->setOslShaderOutput(outputName, outputType);

                        // Set scene template file. For now we only have the constant color scene file
                        mx::FilePath sceneTemplatePath = mx::FilePath::getCurrentPath() / mx::FilePath("resources/Materials/TestSuite/Utilities/");
                        sceneTemplatePath = sceneTemplatePath / sceneTemplateFile;
                        _renderer->setOslTestRenderSceneTemplateFile(sceneTemplatePath.asString());

                        // Validate rendering
                        {
                            RenderUtil::AdditiveScopedTimer renderTimer(profileTimes.languageTimes.renderTime, "OSL render time");
                            _renderer->render();
                            if (imageVec)
                            {
                                imageVec->push_back(_renderer->captureImage());
                            }
                        }
                    }
                    else
                    {
                        CHECK(false);
                        log << ">> Shader has no output to render from\n";
                    }
                }

                validated = true;
            }
            catch (mx::ExceptionRenderError& e)
            {
                // Always dump shader on error
                std::ofstream file;
                file.open(shaderPath + ".osl");
                file << shader->getSourceCode();
                file.close();

                for (const auto& error : e.errorLog())
                {
                    log << e.what() << " " << error << std::endl;
                }
                log << ">> Refer to shader code in dump file: " << shaderPath << ".osl file" << std::endl;
            }
            catch (mx::Exception& e)
            {
                log << e.what();
            }
            CHECK(validated);
        }
    }

    return true;
}

TEST_CASE("Render: OSL TestSuite", "[renderosl]")
{
    if (std::string(MATERIALX_TESTOSLC_EXECUTABLE).empty() &&
        std::string(MATERIALX_TESTRENDER_EXECUTABLE).empty())
    {
        INFO("Skipping the OSL test suite as its executable locations haven't been set.");
        return;
    }

    OslShaderRenderTester renderTester(mx::OslShaderGenerator::create());

    const mx::FilePath testRootPath = mx::FilePath::getCurrentPath() / mx::FilePath("resources/Materials/TestSuite");
    const mx::FilePath testRootPath2 = mx::FilePath::getCurrentPath() / mx::FilePath("resources/Materials/Examples/StandardSurface");
    const mx::FilePath testRootPath3 = mx::FilePath::getCurrentPath() / mx::FilePath("resources/Materials/Examples/UsdPreviewSurface");
    mx::FilePathVec testRootPaths;
    testRootPaths.push_back(testRootPath);
    testRootPaths.push_back(testRootPath2);
    testRootPaths.push_back(testRootPath3);

    mx::FilePath optionsFilePath = testRootPath / mx::FilePath("_options.mtlx");

    renderTester.validate(testRootPaths, optionsFilePath);
}
