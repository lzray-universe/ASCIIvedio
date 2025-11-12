#include "pipeline.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

namespace asciiplay {

Pipeline::Pipeline() = default;
Pipeline::~Pipeline() { stop(); }

bool Pipeline::initialize(const DecoderOptions& decOpt, const PipelineConfig& config, std::string& err)
{
    config_ = config;
    renderer_.configure(config.renderer);

    if (!decoder_.open(decOpt, err)) {
        return false;
    }

    if (!config.exportEnabled) {
        if (!terminal_.initialize()) {
            err = "Failed to initialize terminal";
            return false;
        }
    }

    if (config.exportEnabled) {
        if (!exporter_.open(config.exporter, err)) {
            return false;
        }
    }

    if (config.audio.enabled) {
        if (!audio_.start(48000, 2, config.audio, err)) {
            std::cerr << "Audio disabled: " << err << std::endl;
            config_.audio.enabled = false;
        }
    }

    return true;
}

void Pipeline::run()
{
    running_.store(true);
    startTime_ = std::chrono::steady_clock::now();
    decoder_.start();
    decodeWorker_ = std::thread(&Pipeline::decodeThread, this);
    asciiWorker_ = std::thread(&Pipeline::asciiThread, this);
    renderWorker_ = std::thread(&Pipeline::renderThread, this);
    audioWorker_ = std::thread(&Pipeline::audioThread, this);
    controlWorker_ = std::thread(&Pipeline::controlThread, this);

    decodeWorker_.join();
    asciiWorker_.join();
    renderWorker_.join();
    audioWorker_.join();
    running_.store(false);
    videoCv_.notify_all();
    asciiCv_.notify_all();
    if (controlWorker_.joinable()) controlWorker_.join();
}

void Pipeline::stop()
{
    running_.store(false);
    decoder_.stop();
    videoCv_.notify_all();
    asciiCv_.notify_all();
    if (decodeWorker_.joinable()) decodeWorker_.join();
    if (asciiWorker_.joinable()) asciiWorker_.join();
    if (renderWorker_.joinable()) renderWorker_.join();
    if (audioWorker_.joinable()) audioWorker_.join();
    if (controlWorker_.joinable()) controlWorker_.join();
    terminal_.teardown();
    audio_.stop();
    exporter_.close();
}

void Pipeline::decodeThread()
{
    while (running_) {
        VideoFrame frame;
        if (!decoder_.popVideoFrame(frame)) {
            break;
        }
        {
            std::lock_guard<std::mutex> lock(videoMutex_);
            videoQueue_.push(std::move(frame));
        }
        videoCv_.notify_one();
    }
    videoCv_.notify_all();
}

void Pipeline::asciiThread()
{
    while (running_) {
        VideoFrame frame;
        {
            std::unique_lock<std::mutex> lock(videoMutex_);
            videoCv_.wait(lock, [&] { return !videoQueue_.empty() || !running_; });
            if (!running_ && videoQueue_.empty()) break;
            frame = std::move(videoQueue_.front());
            videoQueue_.pop();
        }
        AsciiFrame ascii = renderer_.render(frame);
        {
            std::lock_guard<std::mutex> lock(asciiMutex_);
            asciiQueue_.push(std::move(ascii));
        }
        asciiCv_.notify_one();
    }
    asciiCv_.notify_all();
}

void Pipeline::renderThread()
{
    auto clockStart = std::chrono::steady_clock::now();
    while (running_) {
        AsciiFrame frame;
        {
            std::unique_lock<std::mutex> lock(asciiMutex_);
            asciiCv_.wait(lock, [&] { return !asciiQueue_.empty() || !running_; });
            if (!running_ && asciiQueue_.empty()) break;
            frame = std::move(asciiQueue_.front());
            asciiQueue_.pop();
        }

        while (paused_.load() && running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!running_) break;

        if (config_.exportEnabled) {
            std::string err;
            if (!exporter_.writeFrame(frame, err)) {
                std::cerr << "Export error: " << err << std::endl;
            }
        } else {
            double target = frame.pts;
            if (config_.targetFps > 0.0) {
                target = renderedFrames_ / config_.targetFps;
            }
            if (config_.audio.enabled) {
                double audioClock = audio_.playbackTime();
                double diff = target - audioClock;
                if (diff > 0.01) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(diff * 1000)));
                } else if (diff < -0.05) {
                    ++droppedFrames_;
                    continue;
                }
            } else {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - clockStart).count();
                double diff = target - elapsed;
                if (diff > 0) std::this_thread::sleep_for(std::chrono::duration<double>(diff));
            }

            terminal_.present(frame);
        }
        ++renderedFrames_;
        updateStats(frame);
    }
}

void Pipeline::audioThread()
{
    while (running_) {
        AudioFrame frame;
        if (!decoder_.popAudioFrame(frame)) {
            break;
        }
        audio_.enqueue(frame);
    }
}

void Pipeline::controlThread()
{
    while (running_) {
        int key = -1;
#ifdef _WIN32
        if (_kbhit()) {
            key = _getch();
        }
#else
        unsigned char c = 0;
        ssize_t n = ::read(STDIN_FILENO, &c, 1);
        if (n > 0) key = c;
#endif
        if (key < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }
        if (key == ' ') {
            bool newState = !paused_.load();
            paused_.store(newState);
            if (config_.audio.enabled) {
                audio_.setVolume(newState ? 0.0f : config_.audio.volume);
            }
        } else if (key == 'q' || key == 'Q') {
            running_.store(false);
            decoder_.stop();
            videoCv_.notify_all();
            asciiCv_.notify_all();
            break;
        } else if (key == 'c' || key == 'C') {
            renderer_.cycleMode();
        } else if (key == 'd' || key == 'D') {
            renderer_.cycleDither();
        } else if (key == 'g') {
            renderer_.adjustGamma(-0.1f);
        } else if (key == 'G') {
            renderer_.adjustGamma(0.1f);
        } else if (key == 'b') {
            renderer_.adjustContrast(-0.1f);
        } else if (key == 'B') {
            renderer_.adjustContrast(0.1f);
        } else if (key == '1') {
            auto cfg = renderer_.config();
            cfg.mode = RenderMode::Gray;
            renderer_.configure(cfg);
        } else if (key == '2') {
            auto cfg = renderer_.config();
            cfg.mode = RenderMode::ANSI256;
            renderer_.configure(cfg);
        } else if (key == '3') {
            auto cfg = renderer_.config();
            cfg.mode = RenderMode::TrueColor;
            renderer_.configure(cfg);
        } else if (key == 'r' || key == 'R') {
            auto cfg = renderer_.config();
            renderer_.configure(cfg);
        }
    }
}

void Pipeline::updateStats(const AsciiFrame& frame)
{
    if (!config_.showStats) return;
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
    double fps = elapsed > 0 ? renderedFrames_ / elapsed : 0.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << "FPS: " << fps << " Rendered: " << renderedFrames_ << " Dropped: " << droppedFrames_;
    if (paused_.load()) {
        oss << " [Paused]";
    }
    statsLine_ = oss.str();
    if (!config_.exportEnabled) {
        terminal_.printStats(statsLine_);
    } else {
        std::cout << "[Export] " << statsLine_ << "\r";
    }
}

} // namespace asciiplay
