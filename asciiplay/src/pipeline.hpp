#pragma once

#include "ascii_renderer.hpp"
#include "audio_player.hpp"
#include "decoder.hpp"
#include "terminal_sink.hpp"
#include "exporter.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace asciiplay {

struct PipelineConfig {
    RendererConfig renderer;
    AudioConfig audio;
    TerminalConfig terminal;
    bool exportEnabled = false;
    ExportConfig exporter;
    double targetFps = 0.0;
    bool showStats = false;
};

class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    bool initialize(const DecoderOptions& decOpt, const PipelineConfig& config, std::string& err);
    void run();
    void stop();

private:
    void decodeThread();
    void renderThread();
    void asciiThread();
    void audioThread();
    void controlThread();
    void updateStats(const AsciiFrame& frame);

    Decoder decoder_;
    AsciiRenderer renderer_;
    TerminalSink terminal_;
    AudioPlayer audio_;
    Exporter exporter_;

    PipelineConfig config_;

    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::thread decodeWorker_;
    std::thread renderWorker_;
    std::thread asciiWorker_;
    std::thread audioWorker_;
    std::thread controlWorker_;

    std::mutex videoMutex_;
    std::condition_variable videoCv_;
    std::queue<VideoFrame> videoQueue_;

    std::mutex asciiMutex_;
    std::condition_variable asciiCv_;
    std::queue<AsciiFrame> asciiQueue_;

    std::string statsLine_;
    std::chrono::steady_clock::time_point startTime_;
    uint64_t renderedFrames_ = 0;
    uint64_t droppedFrames_ = 0;
};

} // namespace asciiplay
