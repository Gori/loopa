#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <mutex>

namespace loopa::app {

struct SettingsData {
    int          version                             = 1;
    juce::String audioDeviceStateXml;                         // serialized AudioDeviceManager state
    float        masterGain                          = 1.0f;  // linear, 0..1+
    float        metronomeVolume                     = 0.35f; // linear, 0..1
    bool         metronomeAutoOnAfterFirstRecord     = false;
    int          defaultBarsForNewTracks             = 4;     // ∈ {1,2,4,8,16}
};

// Global, persistent application settings. Stored as JSON at
// ~/Library/Application Support/Loopa/settings.json.
class Settings {
public:
    static Settings& instance();

    void load();                // read from disk; defaults on missing/corrupt
    void save() const;          // write current data to disk immediately

    const SettingsData& data() const noexcept;

    // Apply a mutation, then persist. Thread-safe (UI thread).
    void mutate(const std::function<void(SettingsData&)>& fn);

    // Convenience setters that mutate + save + notify listeners.
    void setAudioDeviceStateXml(juce::String xml);
    void setMasterGain(float v);
    void setMetronomeVolume(float v);
    void setMetronomeAutoOnAfterFirstRecord(bool v);
    void setDefaultBarsForNewTracks(int bars);

    // Override the file path (tests only).
    void setFileOverride(juce::File f);

    static juce::File defaultFilePath();

private:
    Settings() = default;
    ~Settings() = default;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    juce::File currentFile() const;

    mutable std::mutex m_mutex;
    SettingsData m_data{};
    juce::File m_fileOverride;
};

}  // namespace loopa::app
