cmake_minimum_required(VERSION 3.0)
project(libmimalloc C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(MI_SECURE            "Use full security mitigations (like guard pages, allocation randomization, double-free mitigation, and free-list corruption detection)" OFF)
option(MI_DEBUG_FULL        "Use full internal heap invariant checking in DEBUG mode (expensive)" OFF)
option(MI_PADDING           "Enable padding to detect heap block overflow (used only in DEBUG mode)" ON)
option(MI_OVERRIDE          "Override the standard malloc interface (e.g. define entry points for malloc() etc)" ON)
option(MI_XMALLOC           "Enable abort() call on memory allocation failure by default" OFF)
option(MI_SHOW_ERRORS       "Show error and warning messages by default (only enabled by default in DEBUG mode)" OFF)
option(MI_USE_CXX           "Use the C++ compiler to compile the library (instead of the C compiler)" OFF)
option(MI_SEE_ASM           "Generate assembly files" OFF)
option(MI_INTERPOSE         "Use interpose to override standard malloc on macOS" ON)
option(MI_OSX_ZONE          "Use malloc zone to override standard malloc on macOS" OFF) # enables interpose as well
option(MI_LOCAL_DYNAMIC_TLS "Use slightly slower, dlopen-compatible TLS mechanism (Unix)" OFF)
option(MI_BUILD_SHARED      "Build shared library" ON)
option(MI_BUILD_STATIC      "Build static library" ON)
option(MI_BUILD_OBJECT      "Build object library" ON)
option(MI_BUILD_TESTS       "Build test executables" ON)
option(MI_DEBUG_TSAN        "Build with thread sanitizer (needs clang)" OFF)
option(MI_DEBUG_UBSAN       "Build with undefined-behavior sanitizer (needs clang++)" OFF)
option(MI_CHECK_FULL        "Use full internal invariant checking in DEBUG mode (deprecated, use MI_DEBUG_FULL instead)" OFF)
option(MI_INSTALL_TOPLEVEL  "Install directly into $CMAKE_INSTALL_PREFIX instead of PREFIX/lib/mimalloc-version" OFF)

include("cmake/mimalloc-config-version.cmake")

set(mi_sources
    src/stats.c
    src/random.c
    src/os.c
    src/bitmap.c
    src/arena.c
    src/segment-cache.c
    src/segment.c
    src/page.c
    src/alloc.c
    src/alloc-aligned.c
    src/alloc-posix.c
    src/heap.c
    src/options.c
    src/init.c)

# -----------------------------------------------------------------------------
# Converience: set default build type depending on the build directory
# -----------------------------------------------------------------------------

if (NOT CMAKE_BUILD_TYPE)
  if ("${CMAKE_BINARY_DIR}" MATCHES ".*(D|d)ebug$" OR  MI_DEBUG_FULL)
    message(STATUS "No build type selected, default to: Debug")
    set(CMAKE_BUILD_TYPE "Debug")
  else()
    message(STATUS "No build type selected, default to: Release")
    set(CMAKE_BUILD_TYPE "Release")
  endif()
endif()

if("${CMAKE_BINARY_DIR}" MATCHES ".*(S|s)ecure$")
  message(STATUS "Default to secure build")
  set(MI_SECURE "ON")
endif()

# -----------------------------------------------------------------------------
# Process options
# -----------------------------------------------------------------------------

if(CMAKE_C_COMPILER_ID MATCHES "MSVC|Intel")
  set(MI_USE_CXX "ON")
endif()

if(MI_OVERRIDE)
  message(STATUS "Override standard malloc (MI_OVERRIDE=ON)")
  if(APPLE)
    if(MI_OSX_ZONE)
      # use zone's on macOS
      message(STATUS "  Use malloc zone to override malloc (MI_OSX_ZONE=ON)")
      list(APPEND mi_sources src/alloc-override-osx.c)
      list(APPEND mi_defines MI_OSX_ZONE=1)
      if(NOT MI_INTERPOSE)
        message(STATUS "  (enabling INTERPOSE as well since zone's require this)")
        set(MI_INTERPOSE "ON")
      endif()
    endif()
    if(MI_INTERPOSE)
      # use interpose on macOS
      message(STATUS "  Use interpose to override malloc (MI_INTERPOSE=ON)")
      list(APPEND mi_defines MI_INTERPOSE)
    endif()
  endif()
endif()

if(MI_SECURE)
  message(STATUS "Set full secure build (MI_SECURE=ON)")
  list(APPEND mi_defines MI_SECURE=4)
endif()

if(MI_SEE_ASM)
  message(STATUS "Generate assembly listings (MI_SEE_ASM=ON)")
  list(APPEND mi_cflags -save-temps)
endif()

if(MI_CHECK_FULL)
  message(STATUS "The MI_CHECK_FULL option is deprecated, use MI_DEBUG_FULL instead")
  set(MI_DEBUG_FULL "ON")
endif()

if(MI_DEBUG_FULL)
  message(STATUS "Set debug level to full internal invariant checking (MI_DEBUG_FULL=ON)")
  list(APPEND mi_defines MI_DEBUG=3)   # full invariant checking
endif()

if(NOT MI_PADDING)
  message(STATUS "Disable padding of heap blocks in debug mode (MI_PADDING=OFF)")
  list(APPEND mi_defines MI_PADDING=0)
endif()

if(MI_XMALLOC)
  message(STATUS "Enable abort() calls on memory allocation failure (MI_XMALLOC=ON)")
  list(APPEND mi_defines MI_XMALLOC=1)
endif()

if(MI_SHOW_ERRORS)
  message(STATUS "Enable printing of error and warning messages by default (MI_SHOW_ERRORS=ON)")
  list(APPEND mi_defines MI_SHOW_ERRORS=1)
endif()

if(MI_DEBUG_TSAN)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Build with thread sanitizer (MI_DEBUG_TSAN=ON)")
    list(APPEND mi_defines MI_TSAN=1)
    list(APPEND mi_cflags -fsanitize=thread -g -O1)
    list(APPEND CMAKE_EXE_LINKER_FLAGS -fsanitize=thread)
  else()
    message(WARNING "Can only use thread sanitizer with clang (MI_DEBUG_TSAN=ON but ignored)")    
  endif()  
endif()

if(MI_DEBUG_UBSAN)
  if(CMAKE_BUILD_TYPE MATCHES "Debug")    
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      message(STATUS "Build with undefined-behavior sanitizer (MI_DEBUG_UBSAN=ON)")
      list(APPEND mi_cflags -fsanitize=undefined -g)
      list(APPEND CMAKE_EXE_LINKER_FLAGS -fsanitize=undefined)
      if (NOT MI_USE_CXX)
        message(STATUS "(switch to use C++ due to MI_DEBUG_UBSAN)")
        set(MI_USE_CXX "ON")
      endif()
    else()
      message(WARNING "Can only use undefined-behavior sanitizer with clang++ (MI_DEBUG_UBSAN=ON but ignored)")    
    endif()  
  else()
    message(WARNING "Can only use thread sanitizer with a debug build (CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")    
  endif()
endif()

if(MI_USE_CXX)
  message(STATUS "Use the C++ compiler to compile (MI_USE_CXX=ON)")
  set_source_files_properties(${mi_sources} PROPERTIES LANGUAGE CXX )
  set_source_files_properties(src/static.c test/test-api.c test/test-stress PROPERTIES LANGUAGE CXX )
  if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    list(APPEND mi_cflags -Wno-deprecated)
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
    list(APPEND mi_cflags -Kc++)
  endif()
endif()

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  list(APPEND mi_cflags -Wall -Wextra -Wno-unknown-pragmas -fvisibility=hidden)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    list(APPEND mi_cflags -Wno-invalid-memory-model)
  endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Intel")
  list(APPEND mi_cflags -Wall -fvisibility=hidden)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU|Intel" AND NOT CMAKE_SYSTEM_NAME MATCHES "Haiku")
  if(MI_LOCAL_DYNAMIC_TLS)
    list(APPEND mi_cflags -ftls-model=local-dynamic)
  else()
    list(APPEND mi_cflags -ftls-model=initial-exec)
  endif()
endif()

if (MSVC AND MSVC_VERSION GREATER_EQUAL 1914)
  list(APPEND mi_cflags /Zc:__cplusplus)
endif()

# Architecture flags
if(${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "arm" AND NOT APPLE)
    check_cxx_compiler_flag(-march=native CXX_SUPPORTS_MARCH_NATIVE)
    if (CXX_SUPPORTS_MARCH_NATIVE)
        list(APPEND mi_cflags -march=native)
    endif()
endif()

# extra needed libraries
if(WIN32)
  list(APPEND mi_libraries psapi shell32 user32 advapi32 bcrypt)
else()
  if(NOT ${CMAKE_C_COMPILER} MATCHES "android")
    list(APPEND mi_libraries pthread)
    find_library(LIBRT rt)
    if(LIBRT)
      list(APPEND mi_libraries ${LIBRT})
    endif()
  endif()
endif()

# -----------------------------------------------------------------------------
# Install and output names
# -----------------------------------------------------------------------------

if (MI_INSTALL_TOPLEVEL)
  set(mi_install_dir "${CMAKE_INSTALL_PREFIX}")
else()
  set(mi_install_dir "${CMAKE_INSTALL_PREFIX}/lib/mimalloc-${mi_version}")
endif()

if(MI_SECURE)
  set(mi_basename "mimalloc-secure")
else()
  set(mi_basename "mimalloc")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LC)
if(NOT(CMAKE_BUILD_TYPE_LC MATCHES "^(release|relwithdebinfo|minsizerel)$"))
  set(mi_basename "${mi_basename}-${CMAKE_BUILD_TYPE_LC}") #append build type (e.g. -debug) if not a release version
endif()
if(MI_BUILD_SHARED)
  list(APPEND mi_build_targets "shared")
endif()
if(MI_BUILD_STATIC)
  list(APPEND mi_build_targets "static")
endif()
if(MI_BUILD_OBJECT)
  list(APPEND mi_build_targets "object")
endif()
if(MI_BUILD_TESTS)
  list(APPEND mi_build_targets "tests")
endif()

message(STATUS "")
message(STATUS "Library base name: ${mi_basename}")
message(STATUS "Build type       : ${CMAKE_BUILD_TYPE_LC}")
if(MI_USE_CXX)
  message(STATUS "Compiler         : ${CMAKE_CXX_COMPILER}")
else()
  message(STATUS "Compiler         : ${CMAKE_C_COMPILER}")
endif()
message(STATUS "Install directory: ${mi_install_dir}")
message(STATUS "Build targets    : ${mi_build_targets}")
message(STATUS "")

# -----------------------------------------------------------------------------
# Main targets
# -----------------------------------------------------------------------------

# shared library
if(MI_BUILD_SHARED)
  add_library(mimalloc SHARED ${mi_sources})
  set_target_properties(mimalloc PROPERTIES VERSION ${mi_version} OUTPUT_NAME ${mi_basename} )
  target_compile_definitions(mimalloc PRIVATE ${mi_defines} MI_SHARED_LIB MI_SHARED_LIB_EXPORT)
  target_compile_options(mimalloc PRIVATE ${mi_cflags})
  target_link_libraries(mimalloc PUBLIC ${mi_libraries})
  target_include_directories(mimalloc PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${mi_install_dir}/include>
  )
  if(WIN32)
    # On windows copy the mimalloc redirection dll too.
    target_link_libraries(mimalloc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bin/mimalloc-redirect.lib)
    add_custom_command(TARGET mimalloc POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/bin/mimalloc-redirect.dll" $<TARGET_FILE_DIR:mimalloc>
      COMMENT "Copy mimalloc-redirect.dll to output directory")
  endif()

  if (MI_INSTALL_TOPLEVEL MATCHES "ON")
    install(TARGETS mimalloc EXPORT mimalloc LIBRARY
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
  else()
    install(TARGETS mimalloc EXPORT mimalloc DESTINATION ${mi_install_dir} LIBRARY)
  endif()
  install(EXPORT mimalloc DESTINATION ${mi_install_dir}/cmake)
endif()

# static library
if (MI_BUILD_STATIC)
  add_library(mimalloc-static STATIC ${mi_sources})
  set_property(TARGET mimalloc-static PROPERTY POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(mimalloc-static PRIVATE ${mi_defines} MI_STATIC_LIB)
  target_compile_options(mimalloc-static PRIVATE ${mi_cflags})
  target_link_libraries(mimalloc-static PUBLIC ${mi_libraries})
  target_include_directories(mimalloc-static PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${mi_install_dir}/include>
  )
  if(WIN32)
    # When building both static and shared libraries on Windows, a static library should use a
    # different output name to avoid the conflict with the import library of a shared one.
    string(REPLACE "mimalloc" "mimalloc-static" mi_output_name ${mi_basename})
    set_target_properties(mimalloc-static PROPERTIES OUTPUT_NAME ${mi_output_name})
  else()
    set_target_properties(mimalloc-static PROPERTIES OUTPUT_NAME ${mi_basename})
  endif()

  install(TARGETS mimalloc-static EXPORT mimalloc DESTINATION ${mi_install_dir})
endif()

# install include files
install(FILES include/mimalloc.h DESTINATION ${mi_install_dir}/include)
install(FILES include/mimalloc-override.h DESTINATION ${mi_install_dir}/include)
install(FILES include/mimalloc-new-delete.h DESTINATION ${mi_install_dir}/include)
install(FILES cmake/mimalloc-config.cmake DESTINATION ${mi_install_dir}/cmake)
install(FILES cmake/mimalloc-config-version.cmake DESTINATION ${mi_install_dir}/cmake)

if(NOT WIN32 AND MI_BUILD_SHARED AND NOT MI_INSTALL_TOPLEVEL)
  # install a symlink in the /usr/local/lib to the versioned library
  set(mi_symlink "${CMAKE_SHARED_MODULE_PREFIX}${mi_basename}${CMAKE_SHARED_LIBRARY_SUFFIX}")
  set(mi_soname "mimalloc-${mi_version}/${mi_symlink}.${mi_version}")
  install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${mi_soname} ${mi_symlink} WORKING_DIRECTORY ${mi_install_dir}/..)")
  install(CODE "MESSAGE(\"-- Symbolic link: ${CMAKE_INSTALL_PREFIX}/lib/${mi_symlink} -> ${mi_soname}\")")
endif()

# single object file for more predictable static overriding
if (MI_BUILD_OBJECT)
  add_library(mimalloc-obj OBJECT src/static.c)
  set_property(TARGET mimalloc-obj PROPERTY POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(mimalloc-obj PRIVATE ${mi_defines})
  target_compile_options(mimalloc-obj PRIVATE ${mi_cflags})
  target_include_directories(mimalloc-obj PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${mi_install_dir}/include>
  )

  # the following seems to lead to cmake warnings/errors on some systems, disable for now :-(
  # install(TARGETS mimalloc-obj EXPORT mimalloc DESTINATION ${mi_install_dir})

  # the FILES expression can also be: $<TARGET_OBJECTS:mimalloc-obj>
  # but that fails cmake versions less than 3.10 so we leave it as is for now
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/mimalloc-obj.dir/src/static.c${CMAKE_C_OUTPUT_EXTENSION}
          DESTINATION ${mi_install_dir}
          RENAME ${mi_basename}${CMAKE_C_OUTPUT_EXTENSION} )
endif()

# -----------------------------------------------------------------------------
# API surface testing
# -----------------------------------------------------------------------------

if (MI_BUILD_TESTS)
  add_executable(mimalloc-test-api test/test-api.c)
  target_compile_definitions(mimalloc-test-api PRIVATE ${mi_defines})
  target_compile_options(mimalloc-test-api PRIVATE ${mi_cflags})
  target_include_directories(mimalloc-test-api PRIVATE include)
  target_link_libraries(mimalloc-test-api PRIVATE mimalloc-static ${mi_libraries})

  add_executable(mimalloc-test-stress test/test-stress.c)
  target_compile_definitions(mimalloc-test-stress PRIVATE ${mi_defines})
  target_compile_options(mimalloc-test-stress PRIVATE ${mi_cflags})
  target_include_directories(mimalloc-test-stress PRIVATE include)
  target_link_libraries(mimalloc-test-stress PRIVATE mimalloc ${mi_libraries})

  enable_testing()
  add_test(test_api, mimalloc-test-api)
  add_test(test_stress, mimalloc-test-stress)
endif()

# -----------------------------------------------------------------------------
# Set override properties
# -----------------------------------------------------------------------------
if (MI_OVERRIDE)
  if (MI_BUILD_SHARED)
    target_compile_definitions(mimalloc PRIVATE MI_MALLOC_OVERRIDE)
  endif()
  if(NOT WIN32)
    # It is only possible to override malloc on Windows when building as a DLL.
    if (MI_BUILD_STATIC)
      target_compile_definitions(mimalloc-static PRIVATE MI_MALLOC_OVERRIDE)
    endif()
    if (MI_BUILD_OBJECT)
      target_compile_definitions(mimalloc-obj PRIVATE MI_MALLOC_OVERRIDE)
    endif()
  endif()
endif()
