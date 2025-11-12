#pragma once

#include "color_lut.hpp"
#include "decoder.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace asciiplay {

struct RendererConfig {
    RenderMode mode = RenderMode::Gray;
    DitherMode dither = DitherMode::Bayer4;
    bool halfBlock = false;
    int gridCols = 120;
    int gridRows = 60;
    float gamma = 2.2f;
    float contrast = 1.0f;
};

struct AsciiCell {
    std::string glyph = " ";
    uint32_t fg = 0xFFFFFF;
    uint32_t bg = 0x000000;
};

struct AsciiFrame {
    int cols = 0;
    int rows = 0;
    bool halfBlock = false;
    double pts = 0.0;
    std::vector<AsciiCell> cells;
    std::string terminalString;
};

class AsciiRenderer {
public:
    AsciiRenderer();

    void configure(const RendererConfig& cfg);
    AsciiFrame render(const VideoFrame& frame);
    void cycleMode();
    void cycleDither();
    void adjustGamma(float delta);
    void adjustContrast(float delta);
    RendererConfig config() const;

private:
    AsciiCell sampleCell(const uint8_t* rgb, int width, int height, int startX, int startY,
                         int cellWidth, int cellHeight, int row, int col) const;
    void buildRamp();

    RendererConfig config_;
    std::vector<char> ramp_;
    mutable std::mutex mutex_;
};

} // namespace asciiplay
