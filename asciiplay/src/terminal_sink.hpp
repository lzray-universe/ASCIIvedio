#pragma once

#include "ascii_renderer.hpp"

#include <atomic>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#endif

namespace asciiplay {

struct TerminalConfig {
    double maxWriteMBps = 100.0;
    bool showStats = false;
};

class TerminalSink {
public:
    TerminalSink();
    ~TerminalSink();

    bool initialize();
    void teardown();
    void present(const AsciiFrame& frame);
    void printStats(const std::string& statsLine);
    void requestResize();

private:
    void enableVirtualTerminal();
    void hideCursor();
    void showCursor();
    void maximizeWindow();
    void enableRawMode();
    void disableRawMode();

    std::atomic<bool> resizeRequested_{false};
    bool initialized_ = false;
    bool rawEnabled_ = false;
#ifdef _WIN32
    DWORD inModeBackup_ = 0;
    HANDLE hIn_ = INVALID_HANDLE_VALUE;
#else
    termios origTermios_{};
    int stdinFlags_ = -1;
#endif
};

} // namespace asciiplay
