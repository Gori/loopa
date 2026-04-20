add_library(LoopaTimbre STATIC
    TimbreTransfer.cpp
)

# LoopaCore publicly exports the src/ include root, so we inherit it by
# linking. libtorch exposes TORCH_LIBRARIES + TORCH_INCLUDE_DIRS.
target_link_libraries(LoopaTimbre
    PUBLIC
        LoopaCore
        ${TORCH_LIBRARIES}
)

target_include_directories(LoopaTimbre SYSTEM PUBLIC ${TORCH_INCLUDE_DIRS})

target_compile_features(LoopaTimbre PUBLIC cxx_std_20)

# libtorch requires exceptions + RTTI. We DO NOT inherit LoopaCore's
# -fno-exceptions/-fno-rtti because those flags are PRIVATE on that target.
# libtorch also passes back a CXX11_ABI flag via TORCH_CXX_FLAGS — apply it.
target_compile_options(LoopaTimbre PRIVATE ${TORCH_CXX_FLAGS})

# Relax the strictest warnings here — libtorch public headers trip several
# of them (sign-conversion, old-style-cast, double-promotion, shadow).
set(LOOPA_TIMBRE_WARNING_FLAGS ${LOOPA_WARNING_FLAGS})
list(REMOVE_ITEM LOOPA_TIMBRE_WARNING_FLAGS
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wdouble-promotion
    -Wshadow
)
target_compile_options(LoopaTimbre PRIVATE ${LOOPA_TIMBRE_WARNING_FLAGS})

if(LOOPA_ENABLE_SANITIZERS)
    target_compile_options(LoopaTimbre PRIVATE ${LOOPA_SAN_COMPILE_FLAGS})
    target_link_options(LoopaTimbre PRIVATE ${LOOPA_SAN_LINK_FLAGS})
endif()
