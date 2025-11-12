#include "decoder.hpp"

#include <chrono>
#include <iostream>

extern "C" {
#include <libavutil/imgutils.h>
}

namespace asciiplay {

namespace {
constexpr size_t kMaxVideoQueue = 8;
constexpr size_t kMaxAudioQueue = 32;
}

Decoder::Decoder() = default;

Decoder::~Decoder()
{
    stop();
    if (swsCtx_) sws_freeContext(swsCtx_);
    if (swrCtx_) swr_free(&swrCtx_);
    if (videoCtx_) avcodec_free_context(&videoCtx_);
    if (audioCtx_) avcodec_free_context(&audioCtx_);
    if (fmtCtx_) avformat_close_input(&fmtCtx_);
}

bool Decoder::open(const DecoderOptions& options, std::string& err)
{
    options_ = options;

    if (avformat_open_input(&fmtCtx_, options.url.c_str(), nullptr, nullptr) < 0) {
        err = "Failed to open input";
        return false;
    }

    if (avformat_find_stream_info(fmtCtx_, nullptr) < 0) {
        err = "Failed to find stream info";
        return false;
    }

    AVCodec* videoCodec = nullptr;
    AVCodec* audioCodec = nullptr;

    videoStream_ = av_find_best_stream(fmtCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
    if (videoStream_ < 0 || !videoCodec) {
        err = "No video stream";
        return false;
    }

    videoCtx_ = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCtx_, fmtCtx_->streams[videoStream_]->codecpar);
    if (avcodec_open2(videoCtx_, videoCodec, nullptr) < 0) {
        err = "Failed to open video codec";
        return false;
    }

    videoTimeBase_ = fmtCtx_->streams[videoStream_]->time_base;
    AVRational frameRate = av_guess_frame_rate(fmtCtx_, fmtCtx_->streams[videoStream_], nullptr);
    if (frameRate.num > 0 && frameRate.den > 0) {
        videoFrameDuration_ = av_q2d({frameRate.den, frameRate.num});
    } else {
        videoFrameDuration_ = av_q2d(videoTimeBase_);
    }

    if (options.enableAudio) {
        audioStream_ = av_find_best_stream(fmtCtx_, AVMEDIA_TYPE_AUDIO, -1, videoStream_, &audioCodec, 0);
        if (audioStream_ >= 0 && audioCodec) {
            audioCtx_ = avcodec_alloc_context3(audioCodec);
            avcodec_parameters_to_context(audioCtx_, fmtCtx_->streams[audioStream_]->codecpar);
            if (avcodec_open2(audioCtx_, audioCodec, nullptr) < 0) {
                std::cerr << "Warning: failed to open audio codec" << std::endl;
                avcodec_free_context(&audioCtx_);
                audioStream_ = -1;
            } else {
                audioTimeBase_ = fmtCtx_->streams[audioStream_]->time_base;
                swrCtx_ = swr_alloc_set_opts(nullptr,
                                             av_get_default_channel_layout(2),
                                             AV_SAMPLE_FMT_S16,
                                             48000,
                                             av_get_default_channel_layout(audioCtx_->channels),
                                             audioCtx_->sample_fmt,
                                             audioCtx_->sample_rate,
                                             0, nullptr);
                if (!swrCtx_ || swr_init(swrCtx_) < 0) {
                    std::cerr << "Warning: failed to init resampler" << std::endl;
                    swr_free(&swrCtx_);
                    avcodec_free_context(&audioCtx_);
                    audioCtx_ = nullptr;
                    audioStream_ = -1;
                }
            }
        }
    }

    swsCtx_ = sws_getContext(videoCtx_->width, videoCtx_->height, videoCtx_->pix_fmt,
                             videoCtx_->width, videoCtx_->height, AV_PIX_FMT_RGB24,
                             SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsCtx_) {
        err = "Failed to create scaler";
        return false;
    }

    return true;
}

void Decoder::start()
{
    running_ = true;
    decodeThread_ = std::thread(&Decoder::decodeLoop, this);
}

void Decoder::stop()
{
    running_ = false;
    if (decodeThread_.joinable()) {
        decodeThread_.join();
    }
    {
        std::lock_guard<std::mutex> lk(videoMutex_);
        finished_ = true;
    }
    videoCv_.notify_all();
    audioCv_.notify_all();
}

void Decoder::pushVideoFrame(const VideoFrame& frame)
{
    std::unique_lock<std::mutex> lock(videoMutex_);
    videoCv_.wait(lock, [&] { return videoQueue_.size() < kMaxVideoQueue || !running_; });
    if (!running_) return;
    videoQueue_.push(frame);
    stats_.videoFrames++;
    lock.unlock();
    videoCv_.notify_one();
}

void Decoder::pushAudioFrame(const AudioFrame& frame)
{
    std::unique_lock<std::mutex> lock(audioMutex_);
    audioCv_.wait(lock, [&] { return audioQueue_.size() < kMaxAudioQueue || !running_; });
    if (!running_) return;
    audioQueue_.push(frame);
    stats_.audioFrames++;
    lock.unlock();
    audioCv_.notify_one();
}

bool Decoder::popVideoFrame(VideoFrame& frame)
{
    std::unique_lock<std::mutex> lock(videoMutex_);
    videoCv_.wait(lock, [&] { return !videoQueue_.empty() || finished_; });
    if (videoQueue_.empty()) return false;
    frame = std::move(videoQueue_.front());
    videoQueue_.pop();
    return true;
}

bool Decoder::popAudioFrame(AudioFrame& frame)
{
    std::unique_lock<std::mutex> lock(audioMutex_);
    audioCv_.wait(lock, [&] { return !audioQueue_.empty() || finished_; });
    if (audioQueue_.empty()) return false;
    frame = std::move(audioQueue_.front());
    audioQueue_.pop();
    return true;
}

void Decoder::decodeLoop()
{
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    std::vector<uint8_t> rgbBuffer(videoCtx_->width * videoCtx_->height * 3);
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer.data(), AV_PIX_FMT_RGB24,
                         videoCtx_->width, videoCtx_->height, 1);

    AVFrame* audioFrame = av_frame_alloc();

    while (running_) {
        if (av_read_frame(fmtCtx_, packet) < 0) {
            break;
        }
        if (packet->stream_index == videoStream_) {
            if (avcodec_send_packet(videoCtx_, packet) == 0) {
                while (avcodec_receive_frame(videoCtx_, frame) == 0) {
                    sws_scale(swsCtx_, frame->data, frame->linesize, 0, videoCtx_->height,
                              rgbFrame->data, rgbFrame->linesize);
                    VideoFrame vf;
                    vf.width = videoCtx_->width;
                    vf.height = videoCtx_->height;
                    vf.data.assign(rgbBuffer.begin(), rgbBuffer.end());
                    int64_t ts = frame->best_effort_timestamp;
                    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
                    vf.pts = ts == AV_NOPTS_VALUE ? 0.0 : ts * av_q2d(videoTimeBase_);
                    pushVideoFrame(vf);
                }
            }
        } else if (packet->stream_index == audioStream_ && audioCtx_) {
            if (avcodec_send_packet(audioCtx_, packet) == 0) {
                while (avcodec_receive_frame(audioCtx_, audioFrame) == 0) {
                    int64_t pts = audioFrame->pts;
                    int outSamples = av_rescale_rnd(swr_get_delay(swrCtx_, audioCtx_->sample_rate) + audioFrame->nb_samples,
                                                    48000, audioCtx_->sample_rate, AV_ROUND_UP);
                    AudioFrame af;
                    af.sampleRate = 48000;
                    af.channels = 2;
                    af.samples.resize(outSamples * af.channels);
                    uint8_t* outPlanes[1] = { reinterpret_cast<uint8_t*>(af.samples.data()) };
                    int converted = swr_convert(swrCtx_, outPlanes, outSamples,
                                                const_cast<const uint8_t**>(audioFrame->data), audioFrame->nb_samples);
                    af.samples.resize(converted * af.channels);
                    af.pts = pts == AV_NOPTS_VALUE ? 0.0 : pts * av_q2d(audioTimeBase_);
                    pushAudioFrame(af);
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_unref(packet);

    running_ = false;
    finished_ = true;
    videoCv_.notify_all();
    audioCv_.notify_all();

    av_frame_free(&frame);
    av_frame_free(&rgbFrame);
    av_frame_free(&audioFrame);
    av_packet_free(&packet);
}

} // namespace asciiplay
