#include "ascii_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace asciiplay {

namespace {
constexpr std::array<char, 10> kRamp{"@%#*+=-:. "};
}

AsciiRenderer::AsciiRenderer()
{
    buildRamp();
}

void AsciiRenderer::configure(const RendererConfig& cfg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = cfg;
    buildRamp();
}

RendererConfig AsciiRenderer::config() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void AsciiRenderer::cycleMode()
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (config_.mode) {
    case RenderMode::Gray:
        config_.mode = RenderMode::ANSI256;
        break;
    case RenderMode::ANSI256:
        config_.mode = RenderMode::TrueColor;
        break;
    case RenderMode::TrueColor:
        config_.mode = RenderMode::Gray;
        break;
    }
}

void AsciiRenderer::cycleDither()
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (config_.dither) {
    case DitherMode::Off:
        config_.dither = DitherMode::Bayer2;
        break;
    case DitherMode::Bayer2:
        config_.dither = DitherMode::Bayer4;
        break;
    case DitherMode::Bayer4:
        config_.dither = DitherMode::Off;
        break;
    }
}

void AsciiRenderer::adjustGamma(float delta)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_.gamma = std::clamp(config_.gamma + delta, 0.5f, 4.0f);
}

void AsciiRenderer::adjustContrast(float delta)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_.contrast = std::clamp(config_.contrast + delta, 0.2f, 3.0f);
}

void AsciiRenderer::buildRamp()
{
    ramp_.assign(kRamp.begin(), kRamp.end());
}

AsciiCell AsciiRenderer::sampleCell(const uint8_t* rgb, int width, int height, int startX, int startY,
                                    int cellWidth, int cellHeight, int row, int col) const
{
    const BayerMatrix& matrix = bayer_matrix(config_.dither);
    float accumLuma = 0.0f;
    float accumR = 0.0f;
    float accumG = 0.0f;
    float accumB = 0.0f;
    int count = 0;
    for (int y = 0; y < cellHeight; ++y) {
        int yy = std::clamp(startY + y, 0, height - 1);
        for (int x = 0; x < cellWidth; ++x) {
            int xx = std::clamp(startX + x, 0, width - 1);
            const uint8_t* pixel = rgb + (yy * width + xx) * 3;
            accumLuma += luminance(pixel[0], pixel[1], pixel[2]);
            accumR += pixel[0];
            accumG += pixel[1];
            accumB += pixel[2];
            ++count;
        }
    }

    float avgLuma = accumLuma / std::max(1, count);
    float normalized = apply_gamma(avgLuma, config_.gamma);
    normalized = apply_contrast(normalized, config_.contrast);

    int rampIndex = static_cast<int>(normalized * (ramp_.size() - 1) + 0.5f);
    rampIndex = std::clamp(rampIndex, 0, static_cast<int>(ramp_.size()) - 1);

    float threshold = 0.0f;
    if (matrix.size > 1) {
        int idx = (row % matrix.size) * matrix.size + (col % matrix.size);
        threshold = matrix.thresholds[idx];
    }

    AsciiCell cell;
    cell.glyph = std::string(1, ramp_[rampIndex]);

    uint8_t avgR = static_cast<uint8_t>(accumR / std::max(1, count));
    uint8_t avgG = static_cast<uint8_t>(accumG / std::max(1, count));
    uint8_t avgB = static_cast<uint8_t>(accumB / std::max(1, count));

    if (config_.mode == RenderMode::Gray) {
        uint8_t gray = static_cast<uint8_t>(avgLuma);
        cell.fg = pack_rgb(gray, gray, gray);
        cell.bg = pack_rgb(0, 0, 0);
    } else if (config_.mode == RenderMode::ANSI256) {
        int idx = xterm_index_from_rgb(avgR, avgG, avgB);
        const auto& rgbPalette = xterm_palette()[idx];
        cell.fg = pack_rgb(rgbPalette.r, rgbPalette.g, rgbPalette.b);
        cell.bg = pack_rgb(0, 0, 0);
        if (normalized + threshold > 1.0f) {
            cell.glyph = "#";
        }
    } else {
        cell.fg = pack_rgb(avgR, avgG, avgB);
        cell.bg = pack_rgb(0, 0, 0);
    }
    return cell;
}

AsciiFrame AsciiRenderer::render(const VideoFrame& frame)
{
    RendererConfig cfg = config();

    AsciiFrame ascii;
    ascii.cols = cfg.gridCols;
    ascii.rows = cfg.gridRows;
    ascii.halfBlock = cfg.halfBlock;
    ascii.pts = frame.pts;
    ascii.cells.resize(ascii.cols * ascii.rows);

    int cellWidth = frame.width / cfg.gridCols;
    int cellHeight = frame.height / (cfg.halfBlock ? cfg.gridRows * 2 : cfg.gridRows);
    cellWidth = std::max(1, cellWidth);
    cellHeight = std::max(1, cellHeight);

    for (int y = 0; y < ascii.rows; ++y) {
        for (int x = 0; x < ascii.cols; ++x) {
            int startY = cfg.halfBlock ? y * 2 * cellHeight : y * cellHeight;
            AsciiCell cellTop = sampleCell(frame.data.data(), frame.width, frame.height,
                                           x * cellWidth, startY, cellWidth, cellHeight,
                                           y, x);
            AsciiCell cell = cellTop;
            if (cfg.halfBlock) {
                AsciiCell cellBottom = sampleCell(frame.data.data(), frame.width, frame.height,
                                                  x * cellWidth, startY + cellHeight,
                                                  cellWidth, cellHeight, y + 1, x);
                cell.glyph = u8"▄";
                cell.bg = cellTop.fg;
                cell.fg = cellBottom.fg;
            }
            ascii.cells[y * ascii.cols + x] = cell;
        }
    }

    // Build terminal string.
    std::string buffer;
    buffer.reserve(ascii.cols * ascii.rows * 8);
    buffer.append("\x1b[H");
    const auto flushTrueColor = [&](uint32_t color) {
        RGB rgb = unpack_rgb(color);
        buffer.append("\x1b[38;2;");
        buffer.append(std::to_string(rgb.r));
        buffer.push_back(';');
        buffer.append(std::to_string(rgb.g));
        buffer.push_back(';');
        buffer.append(std::to_string(rgb.b));
        buffer.push_back('m');
    };

    for (int y = 0; y < ascii.rows; ++y) {
        uint32_t currentFg = 0xFFFFFFFF;
        uint32_t currentBg = 0x00000000;
        bool haveColor = false;
        for (int x = 0; x < ascii.cols; ++x) {
            const AsciiCell& cell = ascii.cells[y * ascii.cols + x];
            if (cfg.mode == RenderMode::TrueColor) {
                if (!haveColor || cell.fg != currentFg) {
                    flushTrueColor(cell.fg);
                    currentFg = cell.fg;
                    haveColor = true;
                }
            } else if (cfg.mode == RenderMode::ANSI256) {
                int idx = xterm_index_from_rgb((cell.fg >> 16) & 0xFF, (cell.fg >> 8) & 0xFF, cell.fg & 0xFF);
                buffer.append("\x1b[38;5;");
                buffer.append(std::to_string(idx));
                buffer.push_back('m');
            } else {
                uint8_t gray = static_cast<uint8_t>((cell.fg >> 16) & 0xFF);
                buffer.append("\x1b[38;2;");
                buffer.append(std::to_string(gray));
                buffer.push_back(';');
                buffer.append(std::to_string(gray));
                buffer.push_back(';');
                buffer.append(std::to_string(gray));
                buffer.push_back('m');
            }

            if (cfg.halfBlock) {
                if (!haveColor || cell.bg != currentBg) {
                    RGB rgb = unpack_rgb(cell.bg);
                    buffer.append("\x1b[48;2;");
                    buffer.append(std::to_string(rgb.r));
                    buffer.push_back(';');
                    buffer.append(std::to_string(rgb.g));
                    buffer.push_back(';');
                    buffer.append(std::to_string(rgb.b));
                    buffer.push_back('m');
                    currentBg = cell.bg;
                }
            }

            buffer.append(cell.glyph);
        }
        buffer.append("\x1b[0m\r\n");
    }
    ascii.terminalString = std::move(buffer);
    return ascii;
}

} // namespace asciiplay
