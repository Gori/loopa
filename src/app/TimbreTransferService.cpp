#include "TimbreTransferService.h"

#include "Settings.h"

#include "core/AiPreprocess/AiPreprocess.h"
#include "core/Logger.h"
#include "core/Loop.h"
#include "core/Timbre/TimbreTransfer.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <exception>
#include <utility>

#if defined(__APPLE__)
  #include <pthread.h>
#endif

namespace loopa::app {

TimbreTransferService::TimbreTransferService(std::string modelDir,
                                               std::vector<loopa::TimbrePreset> presets)
    : m_modelDir(std::move(modelDir)),
      m_presets(std::move(presets)),
      m_preproc(std::make_unique<loopa::AiPreprocessor>(m_modelDir)) {
    LOG_INFO("TimbreTransferService: constructed — scheduling eager preload ("
             + std::to_string(m_presets.size()) + " presets from " + m_modelDir + ")");
    m_preloadRequested = true;
    m_worker = std::thread([this] { workerMain(); });
    m_cv.notify_one();
}

std::string TimbreTransferService::presetName(int index) const {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) return {};
    return m_presets[static_cast<std::size_t>(index)].name;
}

TimbreTransferService::~TimbreTransferService() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_shutdown = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

bool TimbreTransferService::submit(int trackId,
                                    std::shared_ptr<const loopa::Loop> source,
                                    int timbreIndex,
                                    CompletionCallback completion) {
    if (trackId < 0 || trackId >= loopa::LooperEngine::kNumTracks) {
        LOG_WARN("TimbreTransferService::submit: invalid trackId=" + std::to_string(trackId));
        return false;
    }
    if (!source || source->empty()) {
        LOG_WARN("TimbreTransferService::submit: empty source loop on track "
                 + std::to_string(trackId));
        return false;
    }

    const auto ix = static_cast<std::size_t>(trackId);
    bool expected = false;
    if (!m_busy[ix].compare_exchange_strong(expected, true)) {
        LOG_INFO("TimbreTransferService::submit: track " + std::to_string(trackId)
                 + " already converting, ignoring click");
        return false;
    }

    LOG_INFO("TimbreTransferService::submit: track " + std::to_string(trackId)
             + ", source length=" + std::to_string(source->length())
             + " samples (" + std::to_string(static_cast<double>(source->length())
                                             / source->sampleRate())
             + " s), bars=" + std::to_string(source->bars())
             + ", bpm=" + std::to_string(source->bpm()));

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.push({trackId, std::move(source), timbreIndex, std::move(completion)});
    }
    m_cv.notify_one();
    return true;
}

bool TimbreTransferService::isBusy(int trackId) const noexcept {
    if (trackId < 0 || trackId >= loopa::LooperEngine::kNumTracks) return false;
    return m_busy[static_cast<std::size_t>(trackId)].load();
}

void TimbreTransferService::deliver(CompletionCallback cb, int trackId,
                                     std::shared_ptr<loopa::Loop> result,
                                     std::string error) {
    const auto ix = static_cast<std::size_t>(trackId);
    m_busy[ix].store(false);

    if (!cb) return;

    juce::MessageManager::callAsync(
        [capturedCb = std::move(cb), trackId,
         capturedResult = std::move(result),
         capturedError = std::move(error)]() mutable {
            capturedCb(trackId, std::move(capturedResult), std::move(capturedError));
        });
}

void TimbreTransferService::workerMain() {
#if defined(__APPLE__)
    // Tell macOS this thread is background work: the scheduler can preempt
    // it whenever real-time threads (audio callback, CoreAudio IO) need
    // cores. Keeps inference from ever causing dropouts.
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif

    // Eager preload: if construction requested it, load the model before
    // waiting for the first submit. Lifts the multi-second first-click
    // stall — after launch the model is already warm when the user clicks.
    bool preload;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        preload = m_preloadRequested;
        m_preloadRequested = false;
    }
    if (preload) {
        LOG_INFO("TimbreTransferService::worker: eager preload starting");
        try {
            m_engine = std::make_unique<loopa::TimbreTransferEngine>(m_modelDir, m_presets);
            m_numTimbres.store(m_engine->numTimbres());
            LOG_INFO("TimbreTransferService::worker: eager preload complete");
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("TimbreTransfer: eager preload failed: ") + e.what());
            // Not fatal — submit will retry and surface a completion error.
        }
    }

    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this] { return m_shutdown || !m_jobs.empty(); });
            if (m_shutdown && m_jobs.empty()) return;
            job = std::move(m_jobs.front());
            m_jobs.pop();
        }

        LOG_INFO("TimbreTransferService::worker: starting job for track "
                 + std::to_string(job.trackId));

        if (!m_engine) {
            try {
                m_engine = std::make_unique<loopa::TimbreTransferEngine>(m_modelDir, m_presets);
            m_numTimbres.store(m_engine->numTimbres());
            } catch (const std::exception& e) {
                LOG_ERROR(std::string("TimbreTransfer: model load failed: ") + e.what());
                deliver(std::move(job.completion), job.trackId, nullptr, e.what());
                continue;
            }
        }

        try {
            // Read pre-process settings at the moment of conversion. If every
            // toggle is off, move the source through unchanged — no copy, no
            // CPU spent. This preserves bit-exact pass-through identical to
            // the pre-AiPreprocess pipeline.
            const auto& s = loopa::app::Settings::instance().data();
            const bool anyPreOn = s.aiPreDenoise || s.aiPreVocalIsolate
                                  || s.aiPreLoudnessNormalize;
            std::shared_ptr<const loopa::Loop> sourceToTransfer;
            if (!anyPreOn) {
                sourceToTransfer = std::move(job.source);
            } else {
                auto mut = job.source->snapshot();
                const auto tPre0 = std::chrono::steady_clock::now();
                m_preproc->process(mut->data(), mut->length(), mut->sampleRate(),
                                   loopa::AiPreprocessor::Options{
                                       s.aiPreDenoise, s.aiPreVocalIsolate,
                                       s.aiPreLoudnessNormalize, -18.0f});
                const auto tPre1 = std::chrono::steady_clock::now();
                LOG_INFO("TimbreTransferService::worker: preprocess "
                         + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              tPre1 - tPre0).count())
                         + " ms (denoise=" + std::to_string(s.aiPreDenoise)
                         + " vocal=" + std::to_string(s.aiPreVocalIsolate)
                         + " lufs=" + std::to_string(s.aiPreLoudnessNormalize) + ")");
                sourceToTransfer = mut;
            }

            const auto t0 = std::chrono::steady_clock::now();
            auto out = m_engine->transfer(std::move(sourceToTransfer), job.timbreIndex);
            const auto t1 = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            LOG_INFO("TimbreTransferService::worker: track " + std::to_string(job.trackId)
                     + " transfer completed in " + std::to_string(elapsedMs)
                     + " ms, output length=" + std::to_string(out ? out->length() : 0)
                     + " samples");
            deliver(std::move(job.completion), job.trackId, std::move(out), {});
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("TimbreTransfer: inference failed: ") + e.what());
            deliver(std::move(job.completion), job.trackId, nullptr, e.what());
        }
    }
}

}  // namespace loopa::app
