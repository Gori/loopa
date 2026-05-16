juce_add_gui_app(LoopaApp
    PRODUCT_NAME "Loopa"
    COMPANY_NAME "Loopa"
    BUNDLE_ID   "co.actnormal.loopa"
    VERSION     "0.1.0"
    APP_SANDBOX_ENABLED FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Loopa records audio through selected inputs."
)

juce_generate_juce_header(LoopaApp)

set(LOOPA_APP_OWN_SOURCES
    Main.cpp
    MainWindow.cpp
    MainComponent.cpp
    EngineHost.cpp
    ChipButton.cpp
    TopBar.cpp
    TrackRow.cpp
    TimbreTransferService.cpp
    Settings.cpp
    SettingsComponent.cpp
    SettingsWindow.cpp
    LogPath.mm
    MetalContext.mm
    WaveformView.mm
)
target_sources(LoopaApp PRIVATE ${LOOPA_APP_OWN_SOURCES})

# Apply strict warnings to our own sources, not JUCE's module sources.
# Drop -Wold-style-cast and -Wsign-conversion: JUCE headers rely on them pervasively,
# so we can't transitively honour them through JUCE includes. LoopaCore keeps the full set.
set(LOOPA_APP_WARNING_FLAGS ${LOOPA_WARNING_FLAGS})
list(REMOVE_ITEM LOOPA_APP_WARNING_FLAGS -Wold-style-cast -Wsign-conversion -Wdouble-promotion -Wconversion)
list(JOIN LOOPA_APP_WARNING_FLAGS ";" LOOPA_APP_WARNING_FLAGS_STR)
set_source_files_properties(${LOOPA_APP_OWN_SOURCES}
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES COMPILE_OPTIONS "${LOOPA_APP_WARNING_FLAGS_STR}"
)

target_compile_definitions(LoopaApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LoopaApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LoopaApp,JUCE_VERSION>"
)

target_link_libraries(LoopaApp
    PRIVATE
        LoopaCore
        LoopaTimbre
        LoopaAiPreprocess
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# libtorch ships multiple dylibs (libtorch, libtorch_cpu, libc10, libomp,
# ...). We bundle the whole lib/ directory into the .app's Frameworks folder
# and set an rpath so the executable can locate them at runtime. Same
# strategy for the .ts model file.
target_link_options(LoopaApp PRIVATE
    "LINKER:-rpath,@executable_path/../Frameworks"
)

file(GLOB LOOPA_LIBTORCH_DYLIBS "${libtorch_SOURCE_DIR}/lib/*.dylib")
add_custom_command(TARGET LoopaApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_CONTENT_DIR:LoopaApp>/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LOOPA_LIBTORCH_DYLIBS}
        "$<TARGET_BUNDLE_CONTENT_DIR:LoopaApp>/Frameworks"
    VERBATIM)

# Bundle every .ts model primitive + every timbre .pt vector.
add_custom_command(TARGET LoopaApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_CONTENT_DIR:LoopaApp>/Resources/models"
    VERBATIM)
file(GLOB LOOPA_MODEL_FILES
    "${LOOPA_MODELS_DIR}/*.ts"
    "${LOOPA_MODELS_DIR}/*.pt")
foreach(mf ${LOOPA_MODEL_FILES})
    get_filename_component(mfname "${mf}" NAME)
    add_custom_command(TARGET LoopaApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${mf}"
            "$<TARGET_BUNDLE_CONTENT_DIR:LoopaApp>/Resources/models/${mfname}"
        VERBATIM)
endforeach()

if(LOOPA_ENABLE_SANITIZERS)
    target_compile_options(LoopaApp PRIVATE ${LOOPA_SAN_COMPILE_FLAGS})
    target_link_options(LoopaApp PRIVATE ${LOOPA_SAN_LINK_FLAGS})
endif()
