add_library(LoopaCore STATIC
    Logger.cpp
    BpmDetector.cpp
    Loop.cpp
    LooperEngine.cpp
    Metronome.cpp
)

target_include_directories(LoopaCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(LoopaCore PUBLIC cxx_std_20)

target_compile_options(LoopaCore PRIVATE
    -fno-exceptions
    -fno-rtti
)

loopa_apply_target_flags(LoopaCore)
