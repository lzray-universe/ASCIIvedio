#pragma once

#include "decoder.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "miniaudio.h"

namespace asciiplay {

struct AudioConfig {
    bool enabled = true;
    float volume = 1.0f;
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool start(int sampleRate, int channels, const AudioConfig& cfg, std::string& err);
    void stop();
    void enqueue(const AudioFrame& frame);
    double playbackTime() const;
    void setVolume(float volume);

private:
    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
    void onData(float* output, ma_uint32 frameCount);

    ma_context context_{};
    ma_device device_{};
    bool deviceStarted_ = false;
    AudioConfig config_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<int16_t> queue_;
    std::atomic<uint64_t> samplesPlayed_{0};
};

} // namespace asciiplay
