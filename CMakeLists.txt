cmake_minimum_required(VERSION 3.28)

project(Loopa
    VERSION 0.1.0
    DESCRIPTION "Minimal audio looper with AI style transfer (planned)"
    LANGUAGES C CXX OBJC OBJCXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")

option(LOOPA_WARNINGS_AS_ERRORS "Treat warnings as errors in Loopa targets" ON)
option(LOOPA_ENABLE_SANITIZERS "Enable ASan + UBSan in Loopa targets" OFF)

set(LOOPA_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
)
if(LOOPA_WARNINGS_AS_ERRORS)
    list(APPEND LOOPA_WARNING_FLAGS -Werror)
endif()

set(LOOPA_SAN_COMPILE_FLAGS "")
set(LOOPA_SAN_LINK_FLAGS "")
if(LOOPA_ENABLE_SANITIZERS)
    set(LOOPA_SAN_COMPILE_FLAGS -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
    set(LOOPA_SAN_LINK_FLAGS -fsanitize=address -fsanitize=undefined)
endif()

# Helper: apply Loopa's strict flags to a target.
function(loopa_apply_target_flags target)
    target_compile_options(${target} PRIVATE ${LOOPA_WARNING_FLAGS} ${LOOPA_SAN_COMPILE_FLAGS})
    target_link_options(${target} PRIVATE ${LOOPA_SAN_LINK_FLAGS})
endfunction()

include(FetchContent)

# ---- JUCE -----------------------------------------------------------------
set(JUCE_VERSION 8.0.12)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${JUCE_VERSION}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---- Catch2 ---------------------------------------------------------------
set(CATCH2_VERSION v3.7.1)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        ${CATCH2_VERSION}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

# ---- libtorch (prebuilt, macOS arm64) -------------------------------------
# Used by LoopaTimbre to run RAVE TorchScript models directly. Replaces ONNX
# Runtime — IIL's RAVE v3 TorchScript exports cannot be converted to ONNX
# without the (unpublished) training-time RAVE fork, so we execute them
# natively via libtorch's JIT instead.
set(LOOPA_LIBTORCH_VERSION 2.11.0)
FetchContent_Declare(libtorch
    URL https://download.pytorch.org/libtorch/cpu/libtorch-macos-arm64-${LOOPA_LIBTORCH_VERSION}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(libtorch)

# The prebuilt libtorch_cpu.dylib and libomp.dylib carry a hardcoded absolute
# dependency on /opt/llvm-openmp/lib/libomp.dylib (a path that only exists on
# PyTorch's build machines). Rewrite both so they resolve via @rpath, which
# the app sets to @executable_path/../Frameworks and tests get via the build
# rpath that CMake already adds.
execute_process(
    COMMAND install_name_tool -id @rpath/libomp.dylib
        "${libtorch_SOURCE_DIR}/lib/libomp.dylib"
    OUTPUT_QUIET ERROR_QUIET
)
execute_process(
    COMMAND install_name_tool -change /opt/llvm-openmp/lib/libomp.dylib
        @rpath/libomp.dylib
        "${libtorch_SOURCE_DIR}/lib/libtorch_cpu.dylib"
    OUTPUT_QUIET ERROR_QUIET
)
# install_name_tool invalidates the code signature; macOS kills unsigned /
# invalid-signed dylibs at dlopen. Re-sign ad-hoc so they load.
execute_process(
    COMMAND codesign --force --sign - --timestamp=none
        "${libtorch_SOURCE_DIR}/lib/libomp.dylib"
        "${libtorch_SOURCE_DIR}/lib/libtorch_cpu.dylib"
    OUTPUT_QUIET ERROR_QUIET
)

# libtorch ships its own TorchConfig.cmake. Use that rather than wiring up
# our own imported targets by hand — it sets the full set of torch_cpu /
# c10 / omp dylibs, include dirs, and ABI flags in one call.
list(APPEND CMAKE_PREFIX_PATH "${libtorch_SOURCE_DIR}")
find_package(Torch REQUIRED)

# ISMIR 2024 "Combining audio control and style transfer using latent
# diffusion" model primitives, exported as individual traced .ts files.
# Each is called separately from the C++ Heun sampling loop.
#   ae.ts         — audio autoencoder (encode + decode)
#   enc_t.ts      — timbre encoder   (AE latent -> 32-dim timbre vector)
#   enc_s.ts      — structure encoder (CQT -> 16-dim structure over time)
#   time_xform.ts — CQT transform    (audio -> CQT)
#   denoise.ts    — diffusion denoiser with EDM preconditioning
set(LOOPA_MODELS_DIR "${CMAKE_SOURCE_DIR}/resources/models"
    CACHE PATH "Directory containing the bundled model .ts files + timbre .pt vectors")

# ---- Targets --------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/core/Timbre)
add_subdirectory(src/core/AiPreprocess)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
