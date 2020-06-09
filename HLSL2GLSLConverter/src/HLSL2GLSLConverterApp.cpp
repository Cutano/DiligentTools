// HLSL2GLSLConverterImpl.cpp : Defines the entry point for the console application.
//

#include "HLSL2GLSLConverterApp.h"

#include "Errors.hpp"
#include "HLSL2GLSLConverterImpl.hpp"
#include "RefCntAutoPtr.hpp"
#include "Errors.hpp"
#include "EngineFactoryOpenGL.h"
#include "RefCntAutoPtr.hpp"
#include "DataBlobImpl.hpp"
#include "FileWrapper.hpp"

namespace Diligent
{

HLSL2GLSLConverterApp::HLSL2GLSLConverterApp()
{
#if EXPLICITLY_LOAD_ENGINE_GL_DLL
    // Declare function pointer
    auto GetEngineFactoryOpenGL = LoadGraphicsEngineOpenGL();
    if (GetEngineFactoryOpenGL == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to load OpenGL engine implementation");
        return -1;
    }
#endif
    m_pFactoryGL = GetEngineFactoryOpenGL();
}

void HLSL2GLSLConverterApp::PrintHelp()
{
    LOG_INFO_MESSAGE("Command line arguments:\n");
    LOG_INFO_MESSAGE("-h             Print help message\n");
    LOG_INFO_MESSAGE("-i <filename>  Input file path (relative to the search directories)\n");
    LOG_INFO_MESSAGE("-d <dirname>   Search directory to look for input file path as well as all #include files");
    LOG_INFO_MESSAGE("               Every search directory should be specified using -d argument\n");
    LOG_INFO_MESSAGE("-o <filename>  Output file to write converted GLSL source to\n");
    LOG_INFO_MESSAGE("-e <funcname>  Shader entry point\n");
    LOG_INFO_MESSAGE("-c             Compile converted GLSL shader\n");
    LOG_INFO_MESSAGE("-t <type>      Shader type. Allowed values:");
    LOG_INFO_MESSAGE("                 vs - vertex shader");
    LOG_INFO_MESSAGE("                 ps - pixel shader");
    LOG_INFO_MESSAGE("                 gs - geometry shader");
    LOG_INFO_MESSAGE("                 ds - domain shader");
    LOG_INFO_MESSAGE("                 hs - domain shader");
    LOG_INFO_MESSAGE("                 cs - domain shader\n");
    LOG_INFO_MESSAGE("-noglsldef     Do not include glsl definitions into the converted source\n");
    LOG_INFO_MESSAGE("-nolocations   Do not use shader input/output locations qualifiers.\n"
                     "               Shader stage interface linking will rely on exact name matching.\n");
}

int HLSL2GLSLConverterApp::ParseCmdLine(int argc, char** argv)
{
    for (int a = 1; a < argc; ++a)
    {
        if (_stricmp(argv[a], "-h") == 0)
        {
            PrintHelp();
        }
        else if (_stricmp(argv[a], "-i") == 0 && a + 1 < argc)
        {
            m_InputPath = argv[++a];
        }
        else if (_stricmp(argv[a], "-o") == 0 && a + 1 < argc)
        {
            m_OutputPath = argv[++a];
        }
        else if (_stricmp(argv[a], "-d") == 0 && a + 1 < argc)
        {
            if (!m_SearchDirectories.empty())
                m_SearchDirectories.push_back(';');
            m_SearchDirectories += argv[++a];
        }
        else if (_stricmp(argv[a], "-e") == 0 && a + 1 < argc)
        {
            m_EntryPoint = argv[++a];
        }
        else if (_stricmp(argv[a], "-c") == 0)
        {
            m_CompileShader = true;
        }
        else if (_stricmp(argv[a], "-t") == 0 && a + 1 < argc)
        {
            ++a;
            if (_stricmp(argv[a], "vs") == 0)
                m_ShaderType = SHADER_TYPE_VERTEX;
            else if (_stricmp(argv[a], "gs") == 0)
                m_ShaderType = SHADER_TYPE_GEOMETRY;
            else if (_stricmp(argv[a], "ps") == 0)
                m_ShaderType = SHADER_TYPE_PIXEL;
            else if (_stricmp(argv[a], "hs") == 0)
                m_ShaderType = SHADER_TYPE_HULL;
            else if (_stricmp(argv[a], "ds") == 0)
                m_ShaderType = SHADER_TYPE_DOMAIN;
            else if (_stricmp(argv[a], "cs") == 0)
                m_ShaderType = SHADER_TYPE_COMPUTE;
            else
            {
                LOG_ERROR_MESSAGE("Unknow shader type ", argv[a], "; Allowed values: vs,gs,ps,ds,hs,cs");
                return -1;
            }
        }
        else if (_stricmp(argv[a], "-noglsldef") == 0)
        {
            m_IncludeGLSLDefintions = false;
        }
        else if (_stricmp(argv[a], "-nolocations") == 0)
        {
            m_UseInOutLocations = false;
        }
        else
        {
            LOG_ERROR_MESSAGE("Unknow command line option ", argv[a]);
            return -1;
        }
    }
    return 0;
}

int HLSL2GLSLConverterApp::Convert(IRenderDevice* pDevice)
{
    if (m_InputPath.length() == 0)
    {
        LOG_ERROR_MESSAGE("Input file path not specified; use -i command line option");
        return -1;
    }

    if (m_ShaderType == SHADER_TYPE_UNKNOWN)
    {
        LOG_ERROR_MESSAGE("Shader type not specified; use -t [vs;ps;gs;ds;hs;cs] command line option");
        return -1;
    }

    LOG_INFO_MESSAGE("Converting \'", m_InputPath, "\' to GLSL...");

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pFactoryGL->CreateDefaultShaderSourceStreamFactory(m_SearchDirectories.c_str(), &pShaderSourceFactory);

    RefCntAutoPtr<IFileStream> pInputFileStream;
    pShaderSourceFactory->CreateInputStream(m_InputPath.c_str(), &pInputFileStream);
    if (!pInputFileStream)
    {
        return -1;
    }
    RefCntAutoPtr<Diligent::IDataBlob> pHLSLSourceBlob(MakeNewRCObj<DataBlobImpl>()(0));
    pInputFileStream->ReadBlob(pHLSLSourceBlob);
    auto* HLSLSource = reinterpret_cast<char*>(pHLSLSourceBlob->GetDataPtr());
    auto  SourceLen  = static_cast<Int32>(pHLSLSourceBlob->GetSize());

    const auto&                               Converter = HLSL2GLSLConverterImpl::GetInstance();
    RefCntAutoPtr<IHLSL2GLSLConversionStream> pStream;
    Converter.CreateStream(m_InputPath.c_str(), pShaderSourceFactory, HLSLSource, SourceLen, &pStream);
    RefCntAutoPtr<Diligent::IDataBlob> pGLSLSourceBlob;
    pStream->Convert(m_EntryPoint.c_str(), m_ShaderType, m_IncludeGLSLDefintions, "_sampler", m_UseInOutLocations, &pGLSLSourceBlob);
    if (!pGLSLSourceBlob) return -1;

    LOG_INFO_MESSAGE("Done");

    if (m_OutputPath.length() != 0)
    {
        FileWrapper pOutputFile(m_OutputPath.c_str(), EFileAccessMode::Overwrite);
        if (pOutputFile != nullptr)
        {
            if (!pOutputFile->Write(pGLSLSourceBlob->GetDataPtr(), pGLSLSourceBlob->GetSize()))
            {
                LOG_ERROR_MESSAGE("Failed to write converted source to output file ", m_OutputPath);
                return -1;
            }
        }
        else
        {
            LOG_ERROR_MESSAGE("Failed to open output file ", m_OutputPath);
            return -1;
        }
    }

    if (pDevice != nullptr)
    {
        LOG_INFO_MESSAGE("Compiling entry point \'", m_EntryPoint, "\' in converted file \'", m_InputPath, '\'');

        ShaderCreateInfo ShaderCI;
        ShaderCI.EntryPoint                 = m_EntryPoint.c_str();
        ShaderCI.Desc.ShaderType            = m_ShaderType;
        ShaderCI.Desc.Name                  = "Test shader";
        ShaderCI.Source                     = reinterpret_cast<char*>(pGLSLSourceBlob->GetDataPtr());
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_GLSL;
        ShaderCI.UseCombinedTextureSamplers = true;
        RefCntAutoPtr<IShader> pTestShader;
        pDevice->CreateShader(ShaderCI, &pTestShader);
        if (!pTestShader)
        {
            LOG_ERROR_MESSAGE("Failed to compile converted source \'", m_InputPath, '\'');
            return -1;
        }
        LOG_INFO_MESSAGE("Done");
    }

    return 0;
}

} // namespace Diligent
