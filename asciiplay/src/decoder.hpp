#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace asciiplay {

struct VideoFrame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data; // RGB24
    double pts = 0.0;
};

struct AudioFrame {
    std::vector<int16_t> samples; // interleaved stereo
    int sampleRate = 48000;
    int channels = 2;
    double pts = 0.0;
};

struct DecoderStats {
    double videoFps = 0.0;
    double audioFps = 0.0;
    uint64_t videoFrames = 0;
    uint64_t audioFrames = 0;
};

struct DecoderOptions {
    std::string url;
    bool enableAudio = true;
};

class Decoder {
public:
    Decoder();
    ~Decoder();

    bool open(const DecoderOptions& options, std::string& err);
    void start();
    void stop();

    bool popVideoFrame(VideoFrame& frame);
    bool popAudioFrame(AudioFrame& frame);
    bool isFinished() const { return finished_; }
    AVRational videoTimeBase() const { return videoTimeBase_; }
    AVRational audioTimeBase() const { return audioTimeBase_; }
    double videoFrameDuration() const { return videoFrameDuration_; }
    const DecoderStats& stats() const { return stats_; }

private:
    void decodeLoop();
    void pushVideoFrame(const VideoFrame& frame);
    void pushAudioFrame(const AudioFrame& frame);

    DecoderOptions options_;
    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext* videoCtx_ = nullptr;
    AVCodecContext* audioCtx_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    SwrContext* swrCtx_ = nullptr;
    int videoStream_ = -1;
    int audioStream_ = -1;
    AVRational videoTimeBase_{};
    AVRational audioTimeBase_{};
    double videoFrameDuration_ = 0.0;

    std::thread decodeThread_;
    std::mutex videoMutex_;
    std::mutex audioMutex_;
    std::condition_variable videoCv_;
    std::condition_variable audioCv_;
    std::queue<VideoFrame> videoQueue_;
    std::queue<AudioFrame> audioQueue_;
    bool running_ = false;
    bool finished_ = false;

    DecoderStats stats_{};
};

} // namespace asciiplay
