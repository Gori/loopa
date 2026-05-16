#include <catch2/catch_test_macros.hpp>

#include "app/Settings.h"

#include <juce_core/juce_core.h>

using loopa::app::Settings;

namespace {

juce::File tempSettingsFile() {
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("loopa-settings-tests")
                 .getChildFile("settings.json");
    f.getParentDirectory().deleteRecursively();
    f.getParentDirectory().createDirectory();
    return f;
}

}  // namespace

TEST_CASE("Settings defaults when file missing", "[settings]") {
    auto f = tempSettingsFile();
    Settings::instance().setFileOverride(f);
    Settings::instance().load();

    const auto& d = Settings::instance().data();
    REQUIRE(d.version == 1);
    REQUIRE(d.masterGain == 1.0f);
    REQUIRE(d.metronomeVolume == 0.35f);
    REQUIRE_FALSE(d.metronomeAutoOnAfterFirstRecord);
    REQUIRE(d.defaultBarsForNewTracks == 4);
    REQUIRE(d.audioDeviceStateXml.isEmpty());
    REQUIRE_FALSE(d.aiPreDenoise);
    REQUIRE_FALSE(d.aiPreVocalIsolate);
    REQUIRE_FALSE(d.aiPreLoudnessNormalize);
}

TEST_CASE("Settings round-trip save and load", "[settings]") {
    auto f = tempSettingsFile();
    Settings::instance().setFileOverride(f);
    Settings::instance().load();

    Settings::instance().setMasterGain(0.5f);
    Settings::instance().setMetronomeVolume(0.8f);
    Settings::instance().setMetronomeAutoOnAfterFirstRecord(true);
    Settings::instance().setDefaultBarsForNewTracks(8);
    Settings::instance().setAudioDeviceStateXml(
        "<DEVICESETUP deviceType=\"TestDevice\"/>");
    Settings::instance().setAiPreDenoise(true);
    Settings::instance().setAiPreVocalIsolate(true);
    Settings::instance().setAiPreLoudnessNormalize(true);

    // Wipe in-memory state and re-load from disk.
    Settings::instance().load();
    const auto& d = Settings::instance().data();
    REQUIRE(d.masterGain == 0.5f);
    REQUIRE(d.metronomeVolume == 0.8f);
    REQUIRE(d.metronomeAutoOnAfterFirstRecord);
    REQUIRE(d.defaultBarsForNewTracks == 8);
    REQUIRE(d.audioDeviceStateXml.contains("TestDevice"));
    REQUIRE(d.aiPreDenoise);
    REQUIRE(d.aiPreVocalIsolate);
    REQUIRE(d.aiPreLoudnessNormalize);
}

TEST_CASE("Settings rejects non-allowed bar counts silently", "[settings]") {
    auto f = tempSettingsFile();
    Settings::instance().setFileOverride(f);
    Settings::instance().load();

    Settings::instance().setDefaultBarsForNewTracks(4);
    Settings::instance().setDefaultBarsForNewTracks(5);   // invalid → ignored
    REQUIRE(Settings::instance().data().defaultBarsForNewTracks == 4);

    Settings::instance().setDefaultBarsForNewTracks(16);
    REQUIRE(Settings::instance().data().defaultBarsForNewTracks == 16);
}

TEST_CASE("Settings clamps gain values", "[settings]") {
    auto f = tempSettingsFile();
    Settings::instance().setFileOverride(f);
    Settings::instance().load();

    Settings::instance().setMasterGain(-1.0f);
    REQUIRE(Settings::instance().data().masterGain == 0.0f);

    Settings::instance().setMetronomeVolume(2.0f);
    REQUIRE(Settings::instance().data().metronomeVolume == 1.0f);

    Settings::instance().setMetronomeVolume(-0.5f);
    REQUIRE(Settings::instance().data().metronomeVolume == 0.0f);
}

TEST_CASE("Settings corrupt JSON loads defaults", "[settings]") {
    auto f = tempSettingsFile();
    f.replaceWithText("this is not json");
    Settings::instance().setFileOverride(f);

    Settings::instance().load();
    const auto& d = Settings::instance().data();
    REQUIRE(d.masterGain == 1.0f);
    REQUIRE(d.defaultBarsForNewTracks == 4);
}
