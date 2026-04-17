#include "SettingsWindow.h"

#include "SettingsComponent.h"
#include "Theme.h"

namespace loopa::app {

SettingsWindow::SettingsWindow(EngineBridge& bridge,
                               juce::AudioDeviceManager& deviceManager)
    : juce::DocumentWindow("Loopa Settings",
                           theme::col(theme::kBg0),
                           juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setContentOwned(new SettingsComponent(bridge, deviceManager), true);
    setResizable(false, false);
    centreWithSize(560, 760);
    setVisible(true);
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::closeButtonPressed() {
    setVisible(false);
}

}  // namespace loopa::app
