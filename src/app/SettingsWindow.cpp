#include "SettingsWindow.h"

#include "SettingsComponent.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace loopa::app {

SettingsWindow::SettingsWindow(EngineBridge& bridge,
                               juce::AudioDeviceManager& deviceManager)
    : juce::DocumentWindow("Loopa Settings",
                           theme::col(theme::kBg0),
                           juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);

    // Wrap the (taller-than-window) settings panel in a Viewport so it can
    // scroll vertically. The vertical scrollbar eats ~14 px so the window
    // is opened slightly wider than the panel's preferred 560 px.
    auto* viewport = new juce::Viewport();
    viewport->setScrollBarsShown(true, false);
    viewport->setViewedComponent(new SettingsComponent(bridge, deviceManager),
                                  true);  // takes ownership
    setContentOwned(viewport, true);

    setResizable(true, false);
    setResizeLimits(400, 320, 1600, 1600);
    centreWithSize(580, 760);
    setVisible(true);
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::closeButtonPressed() {
    setVisible(false);
}

}  // namespace loopa::app
