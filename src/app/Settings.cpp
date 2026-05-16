#include "Settings.h"

#include "core/Logger.h"

#include <string>

namespace loopa::app {

namespace {

constexpr const char* kKeyVersion         = "version";
constexpr const char* kKeyDeviceXml       = "audioDeviceStateXml";
constexpr const char* kKeyMasterGain      = "masterGain";
constexpr const char* kKeyMetroVol        = "metronomeVolume";
constexpr const char* kKeyMetroAuto       = "metronomeAutoOnAfterFirstRecord";
constexpr const char* kKeyDefaultBars     = "defaultBarsForNewTracks";
constexpr const char* kKeyAiPreDenoise    = "aiPreDenoise";
constexpr const char* kKeyAiPreVocal      = "aiPreVocalIsolate";
constexpr const char* kKeyAiPreLoudness   = "aiPreLoudnessNormalize";

juce::var toVar(const SettingsData& d) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty(kKeyVersion,         d.version);
    obj->setProperty(kKeyDeviceXml,       d.audioDeviceStateXml);
    obj->setProperty(kKeyMasterGain,      d.masterGain);
    obj->setProperty(kKeyMetroVol,        d.metronomeVolume);
    obj->setProperty(kKeyMetroAuto,       d.metronomeAutoOnAfterFirstRecord);
    obj->setProperty(kKeyDefaultBars,     d.defaultBarsForNewTracks);
    obj->setProperty(kKeyAiPreDenoise,    d.aiPreDenoise);
    obj->setProperty(kKeyAiPreVocal,      d.aiPreVocalIsolate);
    obj->setProperty(kKeyAiPreLoudness,   d.aiPreLoudnessNormalize);
    return juce::var(obj);
}

SettingsData fromVar(const juce::var& v) {
    SettingsData out{};
    if (!v.isObject()) return out;

    if (v.hasProperty(kKeyVersion))       out.version                             = static_cast<int>(v[kKeyVersion]);
    if (v.hasProperty(kKeyDeviceXml))     out.audioDeviceStateXml                 = v[kKeyDeviceXml].toString();
    if (v.hasProperty(kKeyMasterGain))    out.masterGain                          = static_cast<float>(static_cast<double>(v[kKeyMasterGain]));
    if (v.hasProperty(kKeyMetroVol))      out.metronomeVolume                     = static_cast<float>(static_cast<double>(v[kKeyMetroVol]));
    if (v.hasProperty(kKeyMetroAuto))     out.metronomeAutoOnAfterFirstRecord     = static_cast<bool>(v[kKeyMetroAuto]);
    if (v.hasProperty(kKeyDefaultBars))   out.defaultBarsForNewTracks             = static_cast<int>(v[kKeyDefaultBars]);
    if (v.hasProperty(kKeyAiPreDenoise))  out.aiPreDenoise                        = static_cast<bool>(v[kKeyAiPreDenoise]);
    if (v.hasProperty(kKeyAiPreVocal))    out.aiPreVocalIsolate                   = static_cast<bool>(v[kKeyAiPreVocal]);
    if (v.hasProperty(kKeyAiPreLoudness)) out.aiPreLoudnessNormalize              = static_cast<bool>(v[kKeyAiPreLoudness]);

    if (out.masterGain      < 0.0f) out.masterGain = 0.0f;
    if (out.metronomeVolume < 0.0f) out.metronomeVolume = 0.0f;
    if (out.metronomeVolume > 1.0f) out.metronomeVolume = 1.0f;

    static const int kAllowed[] = {1, 2, 4, 8, 16};
    bool ok = false;
    for (int b : kAllowed) if (b == out.defaultBarsForNewTracks) { ok = true; break; }
    if (!ok) out.defaultBarsForNewTracks = 4;

    return out;
}

}  // namespace

Settings& Settings::instance() {
    static Settings s;
    return s;
}

juce::File Settings::defaultFilePath() {
    const auto root = juce::File::getSpecialLocation(
                          juce::File::userApplicationDataDirectory);
    return root.getChildFile("Loopa").getChildFile("settings.json");
}

juce::File Settings::currentFile() const {
    return m_fileOverride.getFullPathName().isNotEmpty()
             ? m_fileOverride : defaultFilePath();
}

void Settings::setFileOverride(juce::File f) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_fileOverride = std::move(f);
}

void Settings::load() {
    std::lock_guard<std::mutex> lk(m_mutex);
    const auto file = currentFile();
    if (!file.existsAsFile()) {
        m_data = SettingsData{};
        return;
    }
    const auto text = file.loadFileAsString();
    juce::var parsed;
    const auto err = juce::JSON::parse(text, parsed);
    if (err.failed() || !parsed.isObject()) {
        LOG_WARN(std::string("Settings: failed to parse ")
                 + file.getFullPathName().toStdString()
                 + "; using defaults");
        m_data = SettingsData{};
        return;
    }
    m_data = fromVar(parsed);
}

void Settings::save() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    const auto file = currentFile();
    file.getParentDirectory().createDirectory();
    const auto text = juce::JSON::toString(toVar(m_data), true);
    if (!file.replaceWithText(text)) {
        LOG_ERROR(std::string("Settings: failed to write ")
                  + file.getFullPathName().toStdString());
    }
}

const SettingsData& Settings::data() const noexcept {
    return m_data;
}

void Settings::mutate(const std::function<void(SettingsData&)>& fn) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        fn(m_data);
    }
    save();
}

void Settings::setAudioDeviceStateXml(juce::String xml) {
    mutate([&](SettingsData& d) { d.audioDeviceStateXml = std::move(xml); });
}

void Settings::setMasterGain(float v) {
    if (v < 0.0f) v = 0.0f;
    mutate([&](SettingsData& d) { d.masterGain = v; });
}

void Settings::setMetronomeVolume(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    mutate([&](SettingsData& d) { d.metronomeVolume = v; });
}

void Settings::setMetronomeAutoOnAfterFirstRecord(bool v) {
    mutate([&](SettingsData& d) { d.metronomeAutoOnAfterFirstRecord = v; });
}

void Settings::setDefaultBarsForNewTracks(int bars) {
    static const int kAllowed[] = {1, 2, 4, 8, 16};
    bool ok = false;
    for (int b : kAllowed) if (b == bars) { ok = true; break; }
    if (!ok) return;
    mutate([&](SettingsData& d) { d.defaultBarsForNewTracks = bars; });
}

void Settings::setAiPreDenoise(bool v) {
    mutate([&](SettingsData& d) { d.aiPreDenoise = v; });
}

void Settings::setAiPreVocalIsolate(bool v) {
    mutate([&](SettingsData& d) { d.aiPreVocalIsolate = v; });
}

void Settings::setAiPreLoudnessNormalize(bool v) {
    mutate([&](SettingsData& d) { d.aiPreLoudnessNormalize = v; });
}

}  // namespace loopa::app
