#include "terminal_sink.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#ifdef _WIN32
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace asciiplay {

TerminalSink::TerminalSink() = default;

TerminalSink::~TerminalSink()
{
    teardown();
}

bool TerminalSink::initialize()
{
    if (initialized_) return true;
    enableVirtualTerminal();
    hideCursor();
    enableRawMode();
    maximizeWindow();
#ifndef _WIN32
    std::cout << "请全屏终端/最大化" << std::endl;
#endif
    initialized_ = true;
    return true;
}

void TerminalSink::teardown()
{
    if (!initialized_) return;
    disableRawMode();
    showCursor();
    std::cout << "\x1b[0m" << std::flush;
    initialized_ = false;
}

void TerminalSink::enableVirtualTerminal()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
#endif
}

void TerminalSink::hideCursor()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 25;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &info);
#else
    std::cout << "\x1b[?25l";
#endif
}

void TerminalSink::showCursor()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 25;
    info.bVisible = TRUE;
    SetConsoleCursorInfo(hOut, &info);
#else
    std::cout << "\x1b[?25h";
#endif
}

void TerminalSink::maximizeWindow()
{
#ifdef _WIN32
    HWND hwnd = GetConsoleWindow();
    if (hwnd) ShowWindow(hwnd, SW_MAXIMIZE);
#else
    // no-op
#endif
}

void TerminalSink::enableRawMode()
{
    if (rawEnabled_) return;
#ifdef _WIN32
    hIn_ = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn_ == INVALID_HANDLE_VALUE) return;
    GetConsoleMode(hIn_, &inModeBackup_);
    DWORD mode = inModeBackup_;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    mode |= ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hIn_, mode);
#else
    if (tcgetattr(STDIN_FILENO, &origTermios_) == 0) {
        termios raw = origTermios_;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        stdinFlags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, stdinFlags_ | O_NONBLOCK);
    }
#endif
    rawEnabled_ = true;
}

void TerminalSink::disableRawMode()
{
    if (!rawEnabled_) return;
#ifdef _WIN32
    if (hIn_ != INVALID_HANDLE_VALUE) {
        SetConsoleMode(hIn_, inModeBackup_);
    }
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &origTermios_);
    if (stdinFlags_ != -1) {
        fcntl(STDIN_FILENO, F_SETFL, stdinFlags_);
    }
#endif
    rawEnabled_ = false;
}

void TerminalSink::present(const AsciiFrame& frame)
{
    if (!initialized_) return;
    std::cout << frame.terminalString;
    std::cout.flush();
}

void TerminalSink::printStats(const std::string& statsLine)
{
    if (!initialized_) return;
    std::cout << "\x1b[s" << "\x1b[H" << statsLine << "\x1b[u";
    std::cout.flush();
}

void TerminalSink::requestResize()
{
    resizeRequested_.store(true);
}

} // namespace asciiplay
