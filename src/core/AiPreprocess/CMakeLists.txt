add_library(LoopaAiPreprocess STATIC
    AiPreprocess.cpp
)

# LoopaCore publicly exports the src/ include root; libtorch exposes
# TORCH_LIBRARIES + TORCH_INCLUDE_DIRS. Same setup as LoopaTimbre.
target_link_libraries(LoopaAiPreprocess
    PUBLIC
        LoopaCore
        ${TORCH_LIBRARIES}
)

target_include_directories(LoopaAiPreprocess SYSTEM PUBLIC ${TORCH_INCLUDE_DIRS})

target_compile_features(LoopaAiPreprocess PUBLIC cxx_std_20)

# libtorch requires exceptions + RTTI. Inherit nothing from LoopaCore's
# PRIVATE -fno-exceptions/-fno-rtti. Apply libtorch's CXX flags (CXX11_ABI).
target_compile_options(LoopaAiPreprocess PRIVATE ${TORCH_CXX_FLAGS})

# libtorch public headers trip the strictest warnings. Same relaxations as
# LoopaTimbre.
set(LOOPA_AIPREPROCESS_WARNING_FLAGS ${LOOPA_WARNING_FLAGS})
list(REMOVE_ITEM LOOPA_AIPREPROCESS_WARNING_FLAGS
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wdouble-promotion
    -Wshadow
)
target_compile_options(LoopaAiPreprocess PRIVATE ${LOOPA_AIPREPROCESS_WARNING_FLAGS})

if(LOOPA_ENABLE_SANITIZERS)
    target_compile_options(LoopaAiPreprocess PRIVATE ${LOOPA_SAN_COMPILE_FLAGS})
    target_link_options(LoopaAiPreprocess PRIVATE ${LOOPA_SAN_LINK_FLAGS})
endif()
