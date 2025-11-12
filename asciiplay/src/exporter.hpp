#pragma once

#include "ascii_renderer.hpp"
#include "decoder.hpp"

#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace asciiplay {

struct ExportConfig {
    std::string outputFile;
    int gridCols = 120;
    int gridRows = 60;
    int fontW = 8;
    int fontH = 16;
    int fps = 30;
    int crf = 18;
};

class Exporter {
public:
    Exporter();
    ~Exporter();

    bool open(const ExportConfig& cfg, std::string& err);
    void close();
    bool writeFrame(const AsciiFrame& frame, std::string& err);

private:
    bool initializeEncoder(std::string& err);
    void blitAscii(const AsciiFrame& frame, std::vector<uint32_t>& buffer);

    ExportConfig config_;
    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVStream* stream_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVFrame* rgbFrame_ = nullptr;
    AVFrame* yuvFrame_ = nullptr;
    int frameIndex_ = 0;
    bool opened_ = false;
    int glyphW_ = 8;
    int glyphH_ = 16;
};

} // namespace asciiplay
