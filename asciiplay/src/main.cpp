#include "ascii_renderer.hpp"
#include "audio_player.hpp"
#include "decoder.hpp"
#include "pipeline.hpp"
#include "terminal_sink.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/log.h>
}

using namespace asciiplay;

namespace {

struct CommandLineOptions {
    std::string input;
    RenderMode mode = RenderMode::ANSI256;
    std::optional<std::pair<int, int>> grid;
    bool halfblock = false;
    std::optional<double> fps;
    bool noAudio = false;
    int volume = 100;
    std::optional<std::string> exportFile;
    std::optional<std::pair<int, int>> exportGrid;
    std::optional<std::pair<int, int>> exportFont;
    int exportCrf = 18;
    std::optional<double> exportFps;
    DitherMode dither = DitherMode::Bayer4;
    float gamma = 2.2f;
    float contrast = 1.0f;
    double maxWrite = 100.0;
    bool stats = false;
};

std::optional<std::pair<int, int>> parseDimension(const std::string& value)
{
    auto pos = value.find('x');
    if (pos == std::string::npos) return std::nullopt;
    try {
        int w = std::stoi(value.substr(0, pos));
        int h = std::stoi(value.substr(pos + 1));
        if (w <= 0 || h <= 0) return std::nullopt;
        return std::make_pair(w, h);
    } catch (...) {
        return std::nullopt;
    }
}

void printHelp()
{
    std::cout << "asciiplay <input> [options]\n"
              << "  --mode {gray,256,truecolor}\n"
              << "  --grid <cols>x<rows>\n"
              << "  --halfblock {on|off}\n"
              << "  --fps <num>\n"
              << "  --no-audio\n"
              << "  --volume <0..200>\n"
              << "  --export <outfile.mp4>\n"
              << "  --export-grid <cols>x<rows>\n"
              << "  --export-font <w>x<h>\n"
              << "  --export-crf <0..51>\n"
              << "  --export-fps <num>\n"
              << "  --dither {off,bayer2,bayer4}\n"
              << "  --gamma <float>\n"
              << "  --contrast <float>\n"
              << "  --maxwrite <MBps>\n"
              << "  --stats\n"
              << "  --help\n";
}

std::optional<CommandLineOptions> parseArgs(int argc, char** argv)
{
    if (argc < 2) {
        printHelp();
        return std::nullopt;
    }

    CommandLineOptions opts;
    opts.input = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto nextValue = [&](std::string& out) -> bool {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };

        if (arg == "--mode") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            if (value == "gray") opts.mode = RenderMode::Gray;
            else if (value == "256") opts.mode = RenderMode::ANSI256;
            else if (value == "truecolor") opts.mode = RenderMode::TrueColor;
            else return std::nullopt;
        } else if (arg == "--grid") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.grid = parseDimension(value);
            if (!opts.grid) return std::nullopt;
        } else if (arg == "--halfblock") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.halfblock = (value == "on");
        } else if (arg == "--fps") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.fps = std::stod(value);
        } else if (arg == "--no-audio") {
            opts.noAudio = true;
        } else if (arg == "--volume") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.volume = std::stoi(value);
        } else if (arg == "--export") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.exportFile = value;
        } else if (arg == "--export-grid") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.exportGrid = parseDimension(value);
            if (!opts.exportGrid) return std::nullopt;
        } else if (arg == "--export-font") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.exportFont = parseDimension(value);
            if (!opts.exportFont) return std::nullopt;
        } else if (arg == "--export-crf") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.exportCrf = std::stoi(value);
        } else if (arg == "--export-fps") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.exportFps = std::stod(value);
        } else if (arg == "--dither") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            if (value == "off") opts.dither = DitherMode::Off;
            else if (value == "bayer2") opts.dither = DitherMode::Bayer2;
            else if (value == "bayer4") opts.dither = DitherMode::Bayer4;
            else return std::nullopt;
        } else if (arg == "--gamma") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.gamma = std::stof(value);
        } else if (arg == "--contrast") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.contrast = std::stof(value);
        } else if (arg == "--maxwrite") {
            std::string value;
            if (!nextValue(value)) return std::nullopt;
            opts.maxWrite = std::stod(value);
        } else if (arg == "--stats") {
            opts.stats = true;
        } else if (arg == "--help") {
            printHelp();
            return std::nullopt;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return std::nullopt;
        }
    }

    return opts;
}

volatile std::sig_atomic_t gInterrupted = 0;

void handleSignal(int)
{
    gInterrupted = 1;
}

} // namespace

int main(int argc, char** argv)
{
    av_log_set_level(AV_LOG_ERROR);
    auto opts = parseArgs(argc, argv);
    if (!opts) {
        return 1;
    }

    std::signal(SIGINT, handleSignal);
#ifndef _WIN32
    std::signal(SIGTERM, handleSignal);
#endif

    DecoderOptions decoderOpt;
    decoderOpt.url = opts->input;
    decoderOpt.enableAudio = !opts->noAudio;

    PipelineConfig pipelineCfg;
    pipelineCfg.renderer.mode = opts->mode;
    pipelineCfg.renderer.dither = opts->dither;
    pipelineCfg.renderer.halfBlock = opts->halfblock;
    pipelineCfg.renderer.gamma = opts->gamma;
    pipelineCfg.renderer.contrast = opts->contrast;
    if (opts->grid) {
        pipelineCfg.renderer.gridCols = opts->grid->first;
        pipelineCfg.renderer.gridRows = opts->grid->second;
    }
    pipelineCfg.audio.enabled = !opts->noAudio;
    pipelineCfg.audio.volume = static_cast<float>(opts->volume) / 100.0f;
    pipelineCfg.terminal.maxWriteMBps = opts->maxWrite;
    pipelineCfg.showStats = opts->stats;
    pipelineCfg.targetFps = opts->fps.value_or(0.0);

    if (opts->exportFile) {
        pipelineCfg.exportEnabled = true;
        pipelineCfg.exporter.outputFile = *opts->exportFile;
        pipelineCfg.exporter.gridCols = opts->exportGrid ? opts->exportGrid->first : pipelineCfg.renderer.gridCols;
        pipelineCfg.exporter.gridRows = opts->exportGrid ? opts->exportGrid->second : pipelineCfg.renderer.gridRows;
        if (opts->exportFont) {
            pipelineCfg.exporter.fontW = opts->exportFont->first;
            pipelineCfg.exporter.fontH = opts->exportFont->second;
        }
        pipelineCfg.exporter.crf = opts->exportCrf;
        pipelineCfg.exporter.fps = opts->exportFps ? static_cast<int>(*opts->exportFps) : (opts->fps ? static_cast<int>(*opts->fps) : 30);
    }

    Pipeline pipeline;
    std::string err;
    if (!pipeline.initialize(decoderOpt, pipelineCfg, err)) {
        std::cerr << "Failed to initialize pipeline: " << err << std::endl;
        return 1;
    }

    pipeline.run();

    return 0;
}
