add_executable(LoopaTests
    test_logger.cpp
    test_lockfree_queue.cpp
    test_audio_buffer.cpp
    test_bpm_detector.cpp
    test_loop.cpp
    test_recording_buffer.cpp
    test_transport.cpp
    test_undo_stack.cpp
    test_midi_map.cpp
    test_track.cpp
    test_looper_engine.cpp
    test_metronome.cpp
    test_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Settings.cpp
)

target_include_directories(LoopaTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(LoopaTests
    PRIVATE
        LoopaCore
        Catch2::Catch2WithMain
        juce::juce_core
)

target_compile_features(LoopaTests PRIVATE cxx_std_20)

set(LOOPA_TEST_WARNING_FLAGS ${LOOPA_WARNING_FLAGS})
list(REMOVE_ITEM LOOPA_TEST_WARNING_FLAGS -Wold-style-cast -Wsign-conversion -Wdouble-promotion -Wconversion)
target_compile_options(LoopaTests PRIVATE ${LOOPA_TEST_WARNING_FLAGS} ${LOOPA_SAN_COMPILE_FLAGS})
target_link_options(LoopaTests PRIVATE ${LOOPA_SAN_LINK_FLAGS})

include(Catch)
catch_discover_tests(LoopaTests)
