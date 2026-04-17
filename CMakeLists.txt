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

# ---- Targets --------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
