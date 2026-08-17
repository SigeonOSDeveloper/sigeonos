#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdio>
#include <mutex>
#include <gtkmm.h>
#include <glibmm/dispatcher.h>

namespace sig {
    // Single-quote a string for safe shell use.
    inline std::string shq(const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        return out + "'";
    }

    // Run a command and capture its combined output (stderr merged).
    inline std::string run_capture(const std::vector<std::string>& args) {
        std::string cmd;
        for (const auto& a : args) cmd += a + " ";
        std::string out;
        char buf[4096];
        std::shared_ptr<FILE> p(popen((cmd + " 2>&1").c_str(), "r"), pclose);
        if (!p) return "";
        while (fgets(buf, sizeof(buf), p.get())) out += buf;
        return out;
    }

    // Run a command and stream output line-by-line to a callback.
    // Returns exit status, or -1 if the process could not be started.
    template <typename Callback>
    inline int run_stream(const std::vector<std::string>& args, Callback&& cb) {
        std::string cmd;
        for (const auto& a : args) cmd += a + " ";
        char buf[4096];
        FILE* f = popen((cmd + " 2>&1").c_str(), "r");
        if (!f) return -1;
        while (fgets(buf, sizeof(buf), f)) {
            cb(buf);
        }
        int rc = pclose(f);
        return (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
    }

    // Trim whitespace from both ends.
    inline std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
}

// A terminal-style output widget fed from worker threads via a dispatcher.
class Term {
public:
    explicit Term(Gtk::TextView& tv) : tv_(tv) {
        disp_.connect([this] {
            Glib::ustring chunk;
            {
                std::lock_guard<std::mutex> lk(m_);
                if (pending_.empty()) return;
                chunk.swap(pending_);
            }
            auto buf = tv_.get_buffer();
            auto end = buf->end();
            buf->insert(end, chunk);
            auto mark = buf->create_mark(buf->end(), false);
            tv_.scroll_to(mark);
        });
    }
    void log(const Glib::ustring& s) {
        std::lock_guard<std::mutex> lk(m_);
        if (pending_.empty()) disp_.emit();
        pending_ += s;
    }
private:
    Gtk::TextView& tv_;
    Glib::Dispatcher disp_;
    Glib::ustring pending_;
    std::mutex m_;
};
