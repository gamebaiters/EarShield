#ifndef EARSHIELD_DEBUG_LOG_H
#define EARSHIELD_DEBUG_LOG_H

#include <string>

namespace earshield_log {
    void init(const std::string& configDir);
    void write(const char* fmt, ...);
    void close();
    void install_crash_handlers();
}

#define ESLOG(...)        ::earshield_log::write(__VA_ARGS__)
#define ESLOG_ENTER(name) ::earshield_log::write("[ENTER] %s  (%s:%d)", name, __FILE__, __LINE__)
#define ESLOG_LEAVE(name) ::earshield_log::write("[LEAVE] %s", name)

#endif
