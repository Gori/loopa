#pragma once

#include "core/AiPreprocess/AiPreprocess.h"
#include "core/LooperEngine.h"
#include "core/Timbre/TimbreTransfer.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace loopa { class Loop; }

namespace loopa::app {

// Async wrapper around TimbreTransferEngine. Runs a single background worker
// thread that pulls jobs off a queue, loads the TorchScript model on first
// use, and delivers completions on the JUCE message thread.
class TimbreTransferService {
public:
    using CompletionCallback = std::function<void(int trackId,
                                                   std::shared_ptr<loopa::Loop> result,
                                                   std::string error)>;

    // modelDir:  directory containing the 5 ISMIR .ts primitives.
    // presets:   list of instrument presets (name + .pt vector path).
    // Both loaded eagerly on the worker thread at service construction so
    // the first click doesn't pay the multi-second load stall.
    TimbreTransferService(std::string modelDir,
                          std::vector<loopa::TimbrePreset> presets);

    // Return the display name of a preset (for UI menu labels).
    std::string presetName(int index) const;
    ~TimbreTransferService();

    TimbreTransferService(const TimbreTransferService&) = delete;
    TimbreTransferService& operator=(const TimbreTransferService&) = delete;

    // UI-thread API. Returns true if the job was accepted; false if this
    // track already has a job in flight (the UI should have disabled the
    // chip). On success, `completion` runs on the JUCE message thread.
    bool submit(int trackId,
                std::shared_ptr<const loopa::Loop> source,
                int timbreIndex,
                CompletionCallback completion);

    bool isBusy(int trackId) const noexcept;

    // Number of timbre presets available. 0 until the worker finishes
    // loading. Safe to call from the UI thread.
    int numTimbres() const noexcept { return m_numTimbres.load(); }

private:
    struct Job {
        int trackId = -1;
        std::shared_ptr<const loopa::Loop> source;
        int timbreIndex = 0;
        CompletionCallback completion;
    };

    void workerMain();
    void deliver(CompletionCallback cb, int trackId,
                  std::shared_ptr<loopa::Loop> result, std::string error);

    std::string m_modelDir;
    std::vector<loopa::TimbrePreset> m_presets;
    std::unique_ptr<loopa::TimbreTransferEngine> m_engine;
    std::unique_ptr<loopa::AiPreprocessor> m_preproc;

    std::array<std::atomic<bool>, loopa::LooperEngine::kNumTracks> m_busy{};

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<Job> m_jobs;
    bool m_shutdown = false;
    bool m_preloadRequested = false;
    std::atomic<int> m_numTimbres{0};

    std::thread m_worker;
};

}  // namespace loopa::app
