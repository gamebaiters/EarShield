#include "debug_log.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <csignal>
#include <exception>

#ifndef _WIN32
#include <execinfo.h>
#include <unistd.h>
#endif

namespace {
    std::mutex g_mu;
    FILE*      g_fp = nullptr;
    std::string g_path;

    void writeRaw(const char* line) {
        if (!g_fp) return;
        fputs(line, g_fp);
        fflush(g_fp);
    }

    void ts(char* buf, size_t n) {
        time_t t = time(nullptr);
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    }

#ifndef _WIN32
    void dumpStack() {
        void* frames[64];
        int n = backtrace(frames, 64);
        if (g_fp) {
            fputs("[STACK]\n", g_fp);
            fflush(g_fp);
            int fd = fileno(g_fp);
            backtrace_symbols_fd(frames, n, fd);
            fsync(fd);
        }
    }

    [[noreturn]] void onTerminate() {
        ::earshield_log::write("[FATAL] std::terminate triggered");
        dumpStack();
        ::earshield_log::close();
        std::abort();
    }

    void onSignal(int sig) {
        ::earshield_log::write("[SIGNAL] caught signal %d", sig);
        dumpStack();
        ::earshield_log::close();
        signal(sig, SIG_DFL);
        raise(sig);
    }
#else
    void onTerminateWin() {
        ::earshield_log::write("[FATAL] std::terminate triggered");
        ::earshield_log::close();
        std::abort();
    }
#endif
}

namespace earshield_log {

void init(const std::string& configDir) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fp) return;
    g_path = configDir;
    if (!g_path.empty() && g_path.back() != '/' && g_path.back() != '\\') g_path.push_back('/');
    g_path += "EarShield_debug.log";
    g_fp = fopen(g_path.c_str(), "a");
    if (!g_fp) return;

    char tbuf[32];
    ts(tbuf, sizeof(tbuf));
    char header[512];
    snprintf(header, sizeof(header),
        "\n=========================================================\n"
        "[%s] EarShield log opened (path=%s)\n"
        "=========================================================\n",
        tbuf, g_path.c_str());
    writeRaw(header);
}

void write(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_fp) return;

    char tbuf[32];
    ts(tbuf, sizeof(tbuf));

    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    fprintf(g_fp, "[%s] %s\n", tbuf, body);
    fflush(g_fp);
}

void close() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fp) {
        char tbuf[32];
        ts(tbuf, sizeof(tbuf));
        fprintf(g_fp, "[%s] EarShield log closed\n", tbuf);
        fflush(g_fp);
        fclose(g_fp);
        g_fp = nullptr;
    }
}

void install_crash_handlers() {
#ifndef _WIN32
    std::set_terminate(onTerminate);
    signal(SIGSEGV, onSignal);
    signal(SIGABRT, onSignal);
    signal(SIGBUS,  onSignal);
    signal(SIGILL,  onSignal);
    signal(SIGFPE,  onSignal);
#else
    std::set_terminate(onTerminateWin);
#endif
}

}
