#include "MainWindow.h"

#include "MainComponent.h"

namespace loopa::app {

MainWindow::MainWindow(const juce::String& name,
                       juce::Colour backgroundColour,
                       int buttonsNeeded,
                       loopa::LooperEngine& engine,
                       juce::AudioDeviceManager& deviceManager)
    : juce::DocumentWindow(name, backgroundColour, buttonsNeeded) {
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(engine, deviceManager), true);
    setResizable(true, true);
    centreWithSize(1200, 720);
    setVisible(true);
}

void MainWindow::closeButtonPressed() {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

}  // namespace loopa::app
