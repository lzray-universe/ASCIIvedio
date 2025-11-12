#include "exporter.hpp"
#include "tiny_font8x16.hpp"
#include "color_lut.hpp"

#include <algorithm>
#include <iostream>

extern "C" {
#include <libavutil/opt.h>
}

namespace asciiplay {

Exporter::Exporter() = default;

Exporter::~Exporter()
{
    close();
}

bool Exporter::open(const ExportConfig& cfg, std::string& err)
{
    config_ = cfg;
    glyphW_ = std::max(4, config_.fontW);
    glyphH_ = std::max(8, config_.fontH);
    if (config_.outputFile.empty()) {
        err = "Empty export filename";
        return false;
    }
    return initializeEncoder(err);
}

void Exporter::close()
{
    if (opened_) {
        av_write_trailer(fmtCtx_);
    }
    if (rgbFrame_) av_frame_free(&rgbFrame_);
    if (yuvFrame_) av_frame_free(&yuvFrame_);
    if (codecCtx_) avcodec_free_context(&codecCtx_);
    if (fmtCtx_) {
        if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmtCtx_->pb);
        }
        avformat_free_context(fmtCtx_);
    }
    if (swsCtx_) sws_freeContext(swsCtx_);
    opened_ = false;
}

bool Exporter::initializeEncoder(std::string& err)
{
    AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    }
    if (!codec) {
        err = "No suitable encoder";
        return false;
    }

    if (avformat_alloc_output_context2(&fmtCtx_, nullptr, nullptr, config_.outputFile.c_str()) < 0 || !fmtCtx_) {
        err = "Failed to allocate output context";
        return false;
    }

    stream_ = avformat_new_stream(fmtCtx_, nullptr);
    if (!stream_) {
        err = "Failed to create stream";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    codecCtx_->codec_id = codec->id;
    codecCtx_->codec_type = AVMEDIA_TYPE_VIDEO;
    codecCtx_->width = config_.gridCols * glyphW_;
    codecCtx_->height = config_.gridRows * glyphH_;
    codecCtx_->time_base = AVRational{1, config_.fps};
    codecCtx_->framerate = AVRational{config_.fps, 1};
    codecCtx_->pix_fmt = codec->pix_fmts ? codec->pix_fmts[0] : AV_PIX_FMT_YUV420P;
    codecCtx_->gop_size = 12;
    codecCtx_->max_b_frames = 2;

    if (codec->id == AV_CODEC_ID_H264 && codecCtx_->priv_data) {
        av_opt_set(codecCtx_->priv_data, "preset", "medium", 0);
        av_opt_set(codecCtx_->priv_data, "crf", std::to_string(config_.crf).c_str(), 0);
    }

    if (fmtCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        err = "Failed to open encoder";
        return false;
    }

    avcodec_parameters_from_context(stream_->codecpar, codecCtx_);

    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmtCtx_->pb, config_.outputFile.c_str(), AVIO_FLAG_WRITE) < 0) {
            err = "Failed to open output file";
            return false;
        }
    }

    if (avformat_write_header(fmtCtx_, nullptr) < 0) {
        err = "Failed to write header";
        return false;
    }

    rgbFrame_ = av_frame_alloc();
    rgbFrame_->format = AV_PIX_FMT_RGB24;
    rgbFrame_->width = codecCtx_->width;
    rgbFrame_->height = codecCtx_->height;
    av_frame_get_buffer(rgbFrame_, 32);

    yuvFrame_ = av_frame_alloc();
    yuvFrame_->format = codecCtx_->pix_fmt;
    yuvFrame_->width = codecCtx_->width;
    yuvFrame_->height = codecCtx_->height;
    av_frame_get_buffer(yuvFrame_, 32);

    swsCtx_ = sws_getContext(codecCtx_->width, codecCtx_->height, AV_PIX_FMT_RGB24,
                             codecCtx_->width, codecCtx_->height, codecCtx_->pix_fmt,
                             SWS_BICUBIC, nullptr, nullptr, nullptr);

    if (!swsCtx_) {
        err = "Failed to create sws context";
        return false;
    }

    opened_ = true;
    return true;
}

bool Exporter::writeFrame(const AsciiFrame& frame, std::string& err)
{
    if (!opened_) {
        err = "Exporter not opened";
        return false;
    }

    std::vector<uint32_t> buffer(rgbFrame_->width * rgbFrame_->height, 0);
    blitAscii(frame, buffer);

    for (int y = 0; y < rgbFrame_->height; ++y) {
        uint8_t* dst = rgbFrame_->data[0] + y * rgbFrame_->linesize[0];
        for (int x = 0; x < rgbFrame_->width; ++x) {
            uint32_t pixel = buffer[y * rgbFrame_->width + x];
            dst[x * 3 + 0] = (pixel >> 16) & 0xFF;
            dst[x * 3 + 1] = (pixel >> 8) & 0xFF;
            dst[x * 3 + 2] = pixel & 0xFF;
        }
    }

    sws_scale(swsCtx_, rgbFrame_->data, rgbFrame_->linesize, 0, rgbFrame_->height,
              yuvFrame_->data, yuvFrame_->linesize);

    yuvFrame_->pts = frameIndex_++;

    if (avcodec_send_frame(codecCtx_, yuvFrame_) < 0) {
        err = "Failed to send frame";
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    while (true) {
        int ret = avcodec_receive_packet(codecCtx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            break;
        }
        if (ret < 0) {
            err = "Failed to receive packet";
            av_packet_free(&pkt);
            return false;
        }
        pkt->stream_index = stream_->index;
        pkt->pts = av_rescale_q(pkt->pts, codecCtx_->time_base, stream_->time_base);
        pkt->dts = av_rescale_q(pkt->dts, codecCtx_->time_base, stream_->time_base);
        pkt->duration = av_rescale_q(pkt->duration, codecCtx_->time_base, stream_->time_base);
        if (av_interleaved_write_frame(fmtCtx_, pkt) < 0) {
            err = "Failed to write packet";
            av_packet_free(&pkt);
            return false;
        }
        av_packet_unref(pkt);
    }
    return true;
}

void Exporter::blitAscii(const AsciiFrame& frame, std::vector<uint32_t>& buffer)
{
    int stride = rgbFrame_->width;
    std::vector<uint32_t> glyphBuffer(font8x16::glyph_width * font8x16::glyph_height);
    for (int y = 0; y < frame.rows; ++y) {
        for (int x = 0; x < frame.cols; ++x) {
            const AsciiCell& cell = frame.cells[y * frame.cols + x];
            char glyph = cell.glyph.empty() ? ' ' : cell.glyph[0];
            if (glyph < 32 || glyph > 126) glyph = '#';
            uint32_t fg = cell.fg;
            uint32_t bg = cell.bg;
            font8x16::blit_glyph(glyphBuffer.data(), font8x16::glyph_width, glyph, fg, bg);
            int baseX = x * glyphW_;
            int baseY = y * glyphH_;
            for (int yy = 0; yy < glyphH_; ++yy) {
                int srcY = yy * font8x16::glyph_height / glyphH_;
                for (int xx = 0; xx < glyphW_; ++xx) {
                    int srcX = xx * font8x16::glyph_width / glyphW_;
                    buffer[(baseY + yy) * stride + (baseX + xx)] = glyphBuffer[srcY * font8x16::glyph_width + srcX];
                }
            }
        }
    }
}

} // namespace asciiplay
