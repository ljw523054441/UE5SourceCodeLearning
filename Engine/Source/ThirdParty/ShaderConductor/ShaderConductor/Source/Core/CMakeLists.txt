# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

if(WIN32)
    set(dxcompilerName "dxcompiler.dll")
    set(runtimeOutputFolder "bin")
    set(copyTargetFolder ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
else()
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(dxcompilerName "libdxcompiler.dylib")
    else()
        set(dxcompilerName "libdxcompiler.so")
    endif()
    set(runtimeOutputFolder "lib")
    set(copyTargetFolder ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
endif()

set(dxcompilerOutput "${copyTargetFolder}/${CMAKE_CFG_INTDIR}/${dxcompilerName}")
add_custom_target(CopyDxcompiler ALL
    COMMAND ${CMAKE_COMMAND} -E copy
        ${SC_BUILD_DIR}/External/DirectXShaderCompiler/${CMAKE_CFG_INTDIR}/${runtimeOutputFolder}/${dxcompilerName}
        ${dxcompilerOutput}
    COMMENT "Copying dxcompiler...")
add_dependencies(CopyDxcompiler dxcompiler)

set_target_properties(CopyDxcompiler PROPERTIES FOLDER "Core")

# UE Change Begin: Replace @rpath with 'install_name_tool' after deployment.
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
if (APPLE)
  set(CMAKE_INSTALL_NAME_DIR "@rpath")
  set(CMAKE_INSTALL_RPATH "RPATH_DUMMY")
endif()
# UE Change End: Replace @rpath with 'install_name_tool' after deployment.

# UE Change Begin: Allow explicit DllShutdown to prevent dangling mutex on Linux
option(SC_EXPLICIT_DLLSHUTDOWN "Enable explicit DllShutdown" OFF)
if(SC_EXPLICIT_DLLSHUTDOWN)
  add_definitions(-DSC_EXPLICIT_DLLSHUTDOWN)
endif()
# UE Change End: Allow explicit DllShutdown to prevent dangling mutex on Linux

set(LIB_NAME ShaderConductor)

set(SOURCE_FILES
    ${SC_ROOT_DIR}/Source/Core/ShaderConductor.cpp
)

set(HEADER_FILES
    ${SC_ROOT_DIR}/Include/ShaderConductor/ShaderConductor.hpp
)

source_group("Source Files" FILES ${SOURCE_FILES})
source_group("Header Files" FILES ${HEADER_FILES})

add_library(${LIB_NAME} "SHARED"
    ${SOURCE_FILES} ${HEADER_FILES}
)

target_include_directories(${LIB_NAME}
    PUBLIC
        ${SC_ROOT_DIR}/Include

    PRIVATE
        ${SC_BUILD_DIR}/External/DirectXShaderCompiler/include
        ${SC_ROOT_DIR}/External/DirectXShaderCompiler/include
)
target_compile_definitions(${LIB_NAME}
    PRIVATE
        -DSHADER_CONDUCTOR_SOURCE
)
if(MSVC)
    target_compile_definitions(${LIB_NAME}
        PRIVATE
            -D_CRT_SECURE_NO_DEPRECATE
            -D_CRT_SECURE_NO_WARNINGS
            -D_SCL_SECURE_NO_DEPRECATE
            -D_SCL_SECURE_NO_WARNINGS
            -D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
    )
endif()
target_link_libraries(${LIB_NAME}
    PRIVATE
        dxcompiler
        LLVMDxcSupport
        LLVMSupport
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-hlsl
        spirv-cross-msl
        spirv-cross-util
        SPIRV-Tools
		# UE Change Begin: Allow optimization after source-to-spirv conversion and before spirv-to-source cross-compilation
        SPIRV-Tools-opt
		# UE Change End: Allow optimization after source-to-spirv conversion and before spirv-to-source cross-compilation
)

add_dependencies(${LIB_NAME} spirv-cross-core spirv-cross-glsl spirv-cross-hlsl spirv-cross-msl)
add_dependencies(${LIB_NAME} CopyDxcompiler)
add_dependencies(${LIB_NAME} SPIRV-Tools)

set_target_properties(${LIB_NAME} PROPERTIES FOLDER "Core")
