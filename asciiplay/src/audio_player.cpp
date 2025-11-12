#define MINIAUDIO_IMPLEMENTATION
#include "audio_player.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace asciiplay {

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer()
{
    stop();
}

bool AudioPlayer::start(int sampleRate, int channels, const AudioConfig& cfg, std::string& err)
{
    config_ = cfg;
    if (!config_.enabled) return true;

    if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
        err = "Failed to init miniaudio context";
        return false;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = channels;
    deviceConfig.sampleRate = sampleRate;
    deviceConfig.dataCallback = &AudioPlayer::dataCallback;
    deviceConfig.pUserData = this;

    if (ma_device_init(&context_, &deviceConfig, &device_) != MA_SUCCESS) {
        err = "Failed to init playback device";
        ma_context_uninit(&context_);
        return false;
    }

    if (ma_device_start(&device_) != MA_SUCCESS) {
        err = "Failed to start playback device";
        ma_device_uninit(&device_);
        ma_context_uninit(&context_);
        return false;
    }
    deviceStarted_ = true;
    return true;
}

void AudioPlayer::stop()
{
    if (deviceStarted_) {
        ma_device_stop(&device_);
        ma_device_uninit(&device_);
        ma_context_uninit(&context_);
        deviceStarted_ = false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    cv_.notify_all();
}

void AudioPlayer::enqueue(const AudioFrame& frame)
{
    if (!config_.enabled) return;
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.insert(queue_.end(), frame.samples.begin(), frame.samples.end());
    cv_.notify_one();
}

double AudioPlayer::playbackTime() const
{
    if (!config_.enabled) {
        return 0.0;
    }
    uint64_t samples = samplesPlayed_.load();
    return static_cast<double>(samples) / static_cast<double>(device_.sampleRate);
}

void AudioPlayer::setVolume(float volume)
{
    config_.volume = volume;
}

void AudioPlayer::dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    auto* self = reinterpret_cast<AudioPlayer*>(device->pUserData);
    if (!self) return;
    self->onData(reinterpret_cast<float*>(output), frameCount);
}

void AudioPlayer::onData(float* output, ma_uint32 frameCount)
{
    size_t samplesRequested = static_cast<size_t>(frameCount) * device_.playback.channels;
    std::vector<float> temp(samplesRequested, 0.0f);

    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        std::fill(output, output + samplesRequested, 0.0f);
        return;
    }

    size_t samplesAvailable = std::min(queue_.size(), samplesRequested);
    for (size_t i = 0; i < samplesAvailable; ++i) {
        temp[i] = static_cast<float>(queue_[i]) / 32768.0f * config_.volume;
    }
    queue_.erase(queue_.begin(), queue_.begin() + samplesAvailable);
    lock.unlock();

    std::fill(output, output + samplesRequested, 0.0f);
    for (size_t i = 0; i < samplesAvailable; ++i) {
        output[i] = temp[i];
    }
    samplesPlayed_.fetch_add(samplesAvailable / device_.playback.channels);
}

} // namespace asciiplay
