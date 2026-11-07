#pragma once

#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <glob.h>
#include <sstream>
#include <iomanip>

namespace Monolith {

// ══════════════════════════════════════════════════════════════════════════════
// DIRECT ZERO-FORK HYPRLAND UNIX SOCKET IPC ENGINE
// Communicates directly with Hyprland's .socket.sock (< 0.05ms, 0 process forks)
// Automatically validates live socket connectivity to ignore dead session dirs.
// ══════════════════════════════════════════════════════════════════════════════
class HyprIPC {
public:
    static bool is_socket_alive(const std::string& path) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        bool ok = (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
        close(fd);
        return ok;
    }

    static std::string find_socket(const std::string& sock_name = ".socket.sock") {
        const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
        std::string base_dir = (runtime_dir && runtime_dir[0] != '\0') ? runtime_dir : "/run/user/1000";
        const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");

        if (sig && sig[0] != '\0') {
            std::string direct = base_dir + "/hypr/" + sig + "/" + sock_name;
            if (is_socket_alive(direct)) {
                return direct;
            }
        }

        // Fallback glob search across active Hyprland instances, testing connectivity
        std::string pattern = base_dir + "/hypr/*/" + sock_name;
        glob_t g{};
        std::string found;
        if (glob(pattern.c_str(), 0, nullptr, &g) == 0) {
            time_t newest_mtime = 0;
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                struct stat st{};
                if (stat(g.gl_pathv[i], &st) == 0 && st.st_mtime >= newest_mtime) {
                    if (is_socket_alive(g.gl_pathv[i])) {
                        newest_mtime = st.st_mtime;
                        found = g.gl_pathv[i];
                    }
                }
            }
        }
        globfree(&g);
        return found;
    }

    static std::string request(const std::string& payload) {
        std::string sock_path = find_socket(".socket.sock");
        if (sock_path.empty()) return "";

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return "";

        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 150000; // 150ms max timeout
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            return "";
        }

        ssize_t written = write(fd, payload.c_str(), payload.size());
        if (written < 0) {
            close(fd);
            return "";
        }

        std::string response;
        char buf[4096];
        while (true) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            response.append(buf, static_cast<size_t>(n));
        }

        close(fd);
        return response;
    }

    static bool dispatch(const std::string& args) {
        std::string res = request("dispatch " + args);
        return (res.find("ok") != std::string::npos);
    }

    static bool keyword(const std::string& key, const std::string& val) {
        std::string res = request("keyword " + key + " " + val);
        return (res.find("ok") != std::string::npos);
    }

    static bool batch(const std::vector<std::string>& commands) {
        if (commands.empty()) return true;
        std::ostringstream ss;
        ss << "[[BATCH]]";
        for (size_t i = 0; i < commands.size(); ++i) {
            if (i > 0) ss << " ; ";
            ss << commands[i];
        }
        std::string res = request(ss.str());
        return !res.empty();
    }

    static bool reload() {
        std::string res = request("reload");
        return (res.find("ok") != std::string::npos);
    }
};

} // namespace Monolith
