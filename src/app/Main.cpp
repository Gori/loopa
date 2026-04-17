#include "EngineHost.h"
#include "LogPath.h"
#include "MainWindow.h"
#include "Settings.h"
#include "core/Logger.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace loopa::app {

class LoopaApplication : public juce::JUCEApplication {
public:
    LoopaApplication() = default;

    const juce::String getApplicationName() override    { return "Loopa"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override {
        loopa::Logger::Config cfg{};
        cfg.logDir = loopa::platform::userLogDir();
        loopa::Logger::instance().init(cfg);
        LOG_INFO("Loopa starting");

        Settings::instance().load();

        m_engineHost = std::make_unique<EngineHost>();
        m_engineHost->start();

        m_mainWindow = std::make_unique<MainWindow>(
            "Loopa",
            juce::Colour(0xFF0B0B0D),
            juce::DocumentWindow::allButtons,
            m_engineHost->engine(),
            m_engineHost->deviceManager());
    }

    void shutdown() override {
        LOG_INFO("Loopa shutting down");
        m_mainWindow.reset();
        if (m_engineHost) {
            m_engineHost->stop();
            m_engineHost.reset();
        }
        loopa::Logger::instance().shutdown();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<EngineHost> m_engineHost;
    std::unique_ptr<MainWindow> m_mainWindow;
};

}  // namespace loopa::app

START_JUCE_APPLICATION(loopa::app::LoopaApplication)
