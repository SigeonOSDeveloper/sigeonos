#include "common.hpp"
#include <thread>
#include <mutex>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <glibmm/dispatcher.h>
#include <glib/gstdio.h>

namespace {

#define INSTALL_DIR "/opt/resolve"
#define BIN_DIR "/usr/local/bin"
#define ROOT_SCRIPT "/tmp/resolve-installer-root.sh"
#define UNINST_SCRIPT "/tmp/resolve-installer-uninstall.sh"
#define LAUNCHER "/tmp/resolve-launcher"

const char* const ARCH_DEPS[] = {
    "mesa", "gstreamer", "gst-plugins-base", "gst-plugins-good",
    "freetype2", "fontconfig", "libxkbcommon", "libxcb",
    "ocl-icd", "openssl", "libxcrypt-compat",
};
constexpr int N_ARCH_DEPS = sizeof(ARCH_DEPS) / sizeof(ARCH_DEPS[0]);

bool resolve_installed() {
    return Glib::file_test(INSTALL_DIR, Glib::FileTest::IS_DIR);
}

std::string get_version() {
    const char* files[] = {"/opt/resolve/Developer/Licensing/version.txt",
                           "/opt/resolve/version.txt",
                           "/opt/resolve/docs/version.txt"};
    for (auto f : files) {
        std::ifstream in(f);
        if (in) {
            std::string v;
            std::getline(in, v);
            v = sig::trim(v);
            if (!v.empty()) return v;
        }
    }
    return "";
}

std::string state_path() {
    return std::string(g_get_home_dir()) + "/.config/resolve-installer/state.json";
}

void save_state(const std::string& version, const std::string& appimage) {
    std::string dir = std::string(g_get_home_dir()) + "/.config/resolve-installer";
    g_mkdir_with_parents(dir.c_str(), 0755);
    char datebuf[64];
    std::time_t now = std::time(nullptr);
    std::strftime(datebuf, sizeof(datebuf), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
    char* json = g_strdup_printf(
        "{\n  \"version\": \"%s\",\n  \"install_dir\": \"%s\",\n"
        "  \"appimage_path\": \"%s\",\n  \"install_date\": \"%s\",\n"
        "  \"deps_installed\": true\n}\n",
        version.c_str(), INSTALL_DIR, appimage.c_str(), datebuf);
    g_file_set_contents(state_path().c_str(), json, -1, nullptr);
    g_free(json);
}

void clear_state() {
    g_unlink(state_path().c_str());
}

long system_ram_gb() {
    return std::atol(sig::trim(sig::run_capture(
        {"awk '/MemTotal/ {printf \"%.0f\", $2/1024/1024}' /proc/meminfo"})).c_str());
}

bool is_pkg_installed(const std::string& pkg) {
    return std::system(("pacman -Qi " + pkg + " >/dev/null 2>&1").c_str()) == 0;
}

std::string missing_deps_list() {
    std::string miss;
    for (int i = 0; i < N_ARCH_DEPS; i++) {
        if (!is_pkg_installed(ARCH_DEPS[i]))
            miss += std::string(miss.empty() ? "" : " ") + ARCH_DEPS[i];
    }
    return miss;
}

std::string dep_install_cmd() {
    std::string miss = missing_deps_list();
    if (miss.empty()) return "";
    return "pacman -Sy --noconfirm " + miss;
}

// Extract AppImage; returns temp dir base, which contains squashfs-root (or files directly).
std::string extract_appimage(const std::string& run_path) {
    char* tmpl = g_strdup("/tmp/resolve-installer-XXXXXX");
    char* dir = g_mkdtemp(tmpl);
    if (!dir) { g_free(tmpl); return ""; }
    std::string tmpdir(dir);
    g_free(dir);

    char* old_cwd = g_get_current_dir();
    g_chdir(tmpdir.c_str());
    sig::run_capture({sig::shq(run_path) + " --appimage-extract 2>&1"});
    g_chdir(old_cwd);
    g_free(old_cwd);

    if (!Glib::file_test(tmpdir, Glib::FileTest::IS_DIR)) return "";
    return tmpdir;
}

void write_launcher(const std::string& bin_rel) {
    (void)bin_rel;
    FILE* f = fopen(LAUNCHER, "w");
    if (!f) return;
    fprintf(f, "#!/bin/bash\n");
    fprintf(f, "exec 2>>/tmp/resolve-launch.log\n");
    fprintf(f, "echo \"== $(date) ==\" >> /tmp/resolve-launch.log\n");
    fprintf(f, "# DaVinci Resolve's bundled Qt5 does not run on Wayland;\n");
    fprintf(f, "# force the X11 backend (XWayland on Wayland sessions).\n");
    fprintf(f, "export QT_QPA_PLATFORM=\"${QT_QPA_PLATFORM:-xcb}\"\n");
    fprintf(f, "export QT_AUTO_SCREEN_SCALE_FACTOR=1\n");
    fprintf(f, "# Installer-style AppImage: its AppRun launches the interactive\n");
    fprintf(f, "# installer, so run the extracted Resolve binary directly (the\n");
    fprintf(f, "# same entry point AppRun uses for its own try-out).\n");
    fprintf(f, "if [ -f \"" INSTALL_DIR "/installer\" ]; then\n");
    fprintf(f, "  export PATH=\"${PATH}:" INSTALL_DIR "\"\n");
    fprintf(f, "  export BMD_PLUGIN_PATH=\"" INSTALL_DIR "/libs/plugins\"\n");
    fprintf(f, "  for bin in \"bin/resolve\" \"bin/Resolve\" \"resolve\" \"Resolve\"; do\n");
    fprintf(f, "    [ -x \"" INSTALL_DIR "/$bin\" ] && exec \"" INSTALL_DIR "/$bin\" \"$@\"\n");
    fprintf(f, "  done\n");
    fprintf(f, "fi\n");
    fprintf(f, "# Portable AppImage: its AppRun sets up APPDIR and libraries.\n");
    fprintf(f, "if [ -x \"" INSTALL_DIR "/AppRun\" ]; then\n");
    fprintf(f, "  exec \"" INSTALL_DIR "/AppRun\" \"$@\"\n");
    fprintf(f, "fi\n");
    fprintf(f, "# Last resort: binary directly with bundled libs on the path.\n");
    fprintf(f, "for libdir in \"" INSTALL_DIR "/usr/lib\" \"" INSTALL_DIR "/lib\" \"" INSTALL_DIR "/libs\"; do\n");
    fprintf(f, "  if [ -d \"$libdir\" ]; then\n");
    fprintf(f, "    export LD_LIBRARY_PATH=\"$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"\n");
    fprintf(f, "    break\n");
    fprintf(f, "  fi\n");
    fprintf(f, "done\n");
    fprintf(f, "for bin in \"bin/resolve\" \"bin/Resolve\" \"resolve\" \"Resolve\" \"usr/bin/resolve\"; do\n");
    fprintf(f, "  [ -x \"" INSTALL_DIR "/$bin\" ] && exec \"" INSTALL_DIR "/$bin\" \"$@\"\n");
    fprintf(f, "done\n");
    fprintf(f, "echo \"Error: DaVinci Resolve binary not found under " INSTALL_DIR "\" >&2\n");
    fprintf(f, "notify-send -i resolve \"DaVinci Resolve\" \"Failed to launch - see /tmp/resolve-launch.log\" 2>/dev/null || true\n");
    fprintf(f, "exit 1\n");
    fclose(f);
    chmod(LAUNCHER, 0755);
}

bool install_rooted(const std::string& appdir, const std::string& dep_cmd, std::string& error) {
    std::string squashfs = appdir + "/squashfs-root";
    const std::string& src = Glib::file_test(squashfs, Glib::FileTest::IS_DIR) ? squashfs : appdir;

    std::string icon_path;
    const char* icons[] = {"DV_Resolve.png", "resolve.png", "Resolve.png", "icon.png"};
    for (auto n : icons) {
        std::string p = src + "/" + n;
        if (Glib::file_test(p, Glib::FileTest::EXISTS)) { icon_path = p; break; }
    }

    std::string bin_path;
    const char* bins[] = {"bin/resolve", "bin/Resolve", "Resolve", "resolve", "DaVinciResolve"};
    for (auto b : bins) {
        std::string p = src + "/" + b;
        if (Glib::file_test(p, Glib::FileTest::EXISTS) ||
            Glib::file_test(p, Glib::FileTest::IS_EXECUTABLE)) { bin_path = p; break; }
    }
    if (bin_path.empty()) {
        std::string out = sig::run_capture({"sh", "-c",
            "find '" + src + "' -maxdepth 3 -name '*esolve*' -type f -executable 2>/dev/null | head -1"});
        out = sig::trim(out);
        if (!out.empty()) bin_path = out;
    }

    std::string bin_rel = "bin/resolve";
    if (!bin_path.empty() && bin_path.size() > src.size() + 1 &&
        bin_path.compare(0, src.size(), src) == 0) {
        std::string rel = bin_path.substr(src.size() + 1);
        if (!rel.empty()) bin_rel = rel;
    }
    write_launcher(bin_rel);

    FILE* sf = fopen(ROOT_SCRIPT, "w");
    if (!sf) { error = "Cannot create install script"; return false; }

    fprintf(sf, "#!/bin/bash\nset -e\n\n");
    if (!dep_cmd.empty()) fprintf(sf, "%s\n\n", dep_cmd.c_str());
    fprintf(sf, "rm -rf '" INSTALL_DIR "'\n\n");
    fprintf(sf, "mkdir -p '" INSTALL_DIR "'\n");
    fprintf(sf, "cp -a '%s'/* '" INSTALL_DIR "'/\n", src.c_str());
    fprintf(sf, "rm -f '" INSTALL_DIR "/README.md' '" INSTALL_DIR "/readme.md'\n");
    fprintf(sf, "find '" INSTALL_DIR "' -type f -perm /111 -print > /tmp/sigeon-resolve-exec.txt\n");
    fprintf(sf, "find '" INSTALL_DIR "' -type f -exec chmod 644 {} \\;\n");
    fprintf(sf, "find '" INSTALL_DIR "' -type d -exec chmod 755 {} \\;\n");
    fprintf(sf, "while IFS= read -r f; do chmod 755 \"$f\"; done < /tmp/sigeon-resolve-exec.txt\n");
    fprintf(sf, "rm -f /tmp/sigeon-resolve-exec.txt\n\n");
    fprintf(sf, "mkdir -p '" INSTALL_DIR "'/configs '" INSTALL_DIR "'/logs '" INSTALL_DIR "'/.license '" INSTALL_DIR "'/.LUT '" INSTALL_DIR "'/Media '" INSTALL_DIR "'/Extras '" INSTALL_DIR "'/Fairlight '" INSTALL_DIR "/Apple Immersive/Calibration'\n");
    fprintf(sf, "chown -R root:root '" INSTALL_DIR "'\n");
    fprintf(sf, "chmod -R a+rX '" INSTALL_DIR "'\n");
    fprintf(sf, "chmod 777 '" INSTALL_DIR "'/configs '" INSTALL_DIR "'/logs '" INSTALL_DIR "'/.license '" INSTALL_DIR "'/.LUT '" INSTALL_DIR "'/Media '" INSTALL_DIR "'/Extras '" INSTALL_DIR "'/Fairlight '" INSTALL_DIR "/Apple Immersive' '" INSTALL_DIR "/Apple Immersive/Calibration'\n");
    fprintf(sf, "cp '" INSTALL_DIR "'/share/log-conf.xml '" INSTALL_DIR "'/configs/ 2>/dev/null || true\n");
    fprintf(sf, "cp '%s' '" BIN_DIR "/resolve-launcher'\n", LAUNCHER);
    fprintf(sf, "chmod 755 '" BIN_DIR "/resolve-launcher'\n");
    fprintf(sf, "ln -sf '" BIN_DIR "/resolve-launcher' '" BIN_DIR "/resolve'\n\n");
    fprintf(sf, "mkdir -p /usr/share/icons/hicolor/256x256/apps\n");
    if (!icon_path.empty())
        fprintf(sf, "cp '%s' /usr/share/icons/hicolor/256x256/apps/resolve.png\n", icon_path.c_str());
    else
        fprintf(sf, "cp '" INSTALL_DIR "/DV_Resolve.png' /usr/share/icons/hicolor/256x256/apps/resolve.png 2>/dev/null || true\n");
    fprintf(sf, "chmod 644 /usr/share/icons/hicolor/256x256/apps/resolve.png\n");
    fprintf(sf, "gtk-update-icon-cache /usr/share/icons/hicolor 2>/dev/null || true\n\n");
    fprintf(sf, "mkdir -p /usr/share/applications\n");
    fprintf(sf, "mkdir -p '/var/BlackmagicDesign/DaVinci Resolve'\n");
    fprintf(sf, "cat > /usr/share/applications/resolve.desktop << 'EOF'\n");
    fprintf(sf, "[Desktop Entry]\n");
    fprintf(sf, "Type=Application\n");
    fprintf(sf, "Name=DaVinci Resolve\n");
    fprintf(sf, "Comment=Professional Video Editing\n");
    fprintf(sf, "Exec=" BIN_DIR "/resolve\n");
    fprintf(sf, "Icon=resolve\n");
    fprintf(sf, "Categories=AudioVideo;Video;\n");
    fprintf(sf, "Terminal=false\n");
    fprintf(sf, "StartupNotify=true\n");
    fprintf(sf, "EOF\n");
    fprintf(sf, "chmod 644 /usr/share/applications/resolve.desktop\n\n");
    fprintf(sf, "update-desktop-database /usr/share/applications 2>/dev/null || true\n");
    fprintf(sf, "echo 'OK'\n");
    fclose(sf);
    chmod(ROOT_SCRIPT, 0755);

    std::string out = sig::run_capture({"pkexec " ROOT_SCRIPT});
    if (out.find("OK") == std::string::npos) {
        error = out.empty() ? "Installation cancelled or failed" : out;
        return false;
    }
    return true;
}

bool uninstall_rooted(std::string& error) {
    FILE* sf = fopen(UNINST_SCRIPT, "w");
    if (!sf) { error = "Cannot create uninstall script"; return false; }
    fprintf(sf, "#!/bin/bash\nset -e\n");
    fprintf(sf, "rm -rf '" INSTALL_DIR "'\n");
    fprintf(sf, "rm -f '" BIN_DIR "/resolve'\n");
    fprintf(sf, "rm -f '" BIN_DIR "/resolve-launcher'\n");
    fprintf(sf, "rm -f /usr/share/applications/resolve.desktop\n");
    fprintf(sf, "rm -f /usr/share/icons/hicolor/256x256/apps/resolve.png\n");
    fprintf(sf, "gtk-update-icon-cache /usr/share/icons/hicolor 2>/dev/null || true\n");
    fprintf(sf, "update-desktop-database /usr/share/applications 2>/dev/null || true\n");
    fprintf(sf, "echo 'OK'\n");
    fclose(sf);
    chmod(UNINST_SCRIPT, 0755);

    std::string out = sig::run_capture({"pkexec " UNINST_SCRIPT});
    if (out.find("OK") == std::string::npos) {
        error = out.empty() ? "Uninstall cancelled or failed" : out;
        return false;
    }
    return true;
}

} // namespace

class DavinciWindow : public Gtk::Window {
public:
    DavinciWindow() {
        set_title("Sigeon DaVinci Installer");
        set_default_size(640, 620);

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        box->set_margin(20);

        auto* title = Gtk::make_managed<Gtk::Label>();
        title->set_markup("<span size='x-large' weight='bold'>DaVinci Resolve Installer</span>");
        title->set_xalign(0);

        long ram = system_ram_gb();
        ram_label_ = Gtk::make_managed<Gtk::Label>();
        ram_label_->set_wrap(true);
        ram_label_->set_xalign(0);
        if (ram > 0 && ram < 32) {
            ram_label_->set_markup(
                "<span weight='bold' fgcolor='#d2691e'>Your system has only " +
                Glib::ustring(std::to_string(ram)) +
                " GB of RAM.</span>\nDaVinci Resolve officially recommends 32 GB; "
                "performance is not guaranteed.");
        } else {
            ram_label_->set_markup("System RAM: <b>" + Glib::ustring(std::to_string(ram)) +
                                   " GB</b> meets the 32 GB recommendation.");
        }

        auto* file_label = Gtk::make_managed<Gtk::Label>(
            "Select the DaVinci Resolve installer (.run / .AppImage from Blackmagic Design).");
        file_label->set_xalign(0);

        file_entry_ = Gtk::make_managed<Gtk::Entry>();
        file_entry_->set_hexpand(true);
        file_entry_->set_placeholder_text("/path/to/DaVinci_Resolve_linux.run");
        file_entry_->signal_changed().connect(sigc::mem_fun(*this, &DavinciWindow::refresh));

        auto* browse = Gtk::make_managed<Gtk::Button>("Browse...");
        browse->signal_clicked().connect(sigc::mem_fun(*this, &DavinciWindow::on_browse));

        auto* file_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        file_row->append(*file_entry_);
        file_row->append(*browse);

        auto* info_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        info_row->set_halign(Gtk::Align::CENTER);
        auto* ver_lbl = Gtk::make_managed<Gtk::Label>("Installed:");
        version_label_ = Gtk::make_managed<Gtk::Label>("Not installed");
        version_label_->set_selectable(true);
        info_row->append(*ver_lbl);
        info_row->append(*version_label_);

        status_label_ = Gtk::make_managed<Gtk::Label>();
        status_label_->set_halign(Gtk::Align::CENTER);
        status_label_->set_wrap(true);

        progress_ = Gtk::make_managed<Gtk::ProgressBar>();
        progress_->set_hexpand(true);
        progress_->set_visible(false);

        auto* btn_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btn_row->set_halign(Gtk::Align::CENTER);

        install_btn_ = Gtk::make_managed<Gtk::Button>("Install");
        upgrade_btn_ = Gtk::make_managed<Gtk::Button>("Upgrade");
        uninstall_btn_ = Gtk::make_managed<Gtk::Button>("Uninstall");
        install_btn_->get_style_context()->add_class("suggested-action");

        install_btn_->signal_clicked().connect(sigc::mem_fun(*this, &DavinciWindow::on_install));
        upgrade_btn_->signal_clicked().connect(sigc::mem_fun(*this, &DavinciWindow::on_install));
        uninstall_btn_->signal_clicked().connect(sigc::mem_fun(*this, &DavinciWindow::on_uninstall));

        btn_row->append(*install_btn_);
        btn_row->append(*upgrade_btn_);
        btn_row->append(*uninstall_btn_);

        auto* tip = Gtk::make_managed<Gtk::Label>(
            "Extracts the AppImage, installs Arch dependencies, then places Resolve into "
            "/opt/resolve with a launcher and desktop entry. One pkexec prompt.");
        tip->set_wrap(true);
        tip->set_xalign(0);
        tip->get_style_context()->add_class("dim-label");

        output_view_ = Gtk::make_managed<Gtk::TextView>();
        output_view_->set_editable(false);
        output_view_->set_monospace(true);
        output_view_->set_wrap_mode(Gtk::WrapMode::CHAR);
        output_view_->set_vexpand(true);
        output_view_->set_size_request(-1, 170);
        auto* oscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        oscroll->set_child(*output_view_);
        auto* oframe = Gtk::make_managed<Gtk::Frame>("Log");
        oframe->set_child(*oscroll);

        term_ = std::make_shared<Term>(*output_view_);

        box->append(*title);
        box->append(*ram_label_);
        box->append(*file_label);
        box->append(*file_row);
        box->append(*info_row);
        box->append(*status_label_);
        box->append(*progress_);
        box->append(*btn_row);
        box->append(*tip);
        box->append(*oframe);
        set_child(*box);

        disp_.connect([this] { drain(); });
        refresh();
    }

    ~DavinciWindow() override = default;

private:
    Gtk::Entry* file_entry_ = nullptr;
    Gtk::Button* install_btn_ = nullptr;
    Gtk::Button* upgrade_btn_ = nullptr;
    Gtk::Button* uninstall_btn_ = nullptr;
    Gtk::Label* ram_label_ = nullptr;
    Gtk::Label* version_label_ = nullptr;
    Gtk::Label* status_label_ = nullptr;
    Gtk::ProgressBar* progress_ = nullptr;
    Gtk::TextView* output_view_ = nullptr;
    std::shared_ptr<Term> term_;
    Glib::RefPtr<Gtk::FileDialog> filedialog_;
    Glib::Dispatcher disp_;
    std::mutex mtx_;
    std::vector<std::function<void()>> queue_;
    bool busy_ = false;
    bool unfinished_ = false;

    void post(const std::function<void()>& fn) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.push_back(fn);
        }
        disp_.emit();
    }

    void drain() {
        std::vector<std::function<void()>> tmp;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            tmp.swap(queue_);
        }
        for (auto& fn : tmp) fn();
    }

    void set_status(const Glib::ustring& s) {
        status_label_->set_text(s);
    }

    void refresh() {
        bool installed = resolve_installed();
        bool has_file = !sig::trim(file_entry_->get_text().raw()).empty();

        if (installed) {
            std::string ver = get_version();
            version_label_->set_text(ver.empty() ? "installed (unknown version)" : "v" + ver);
        } else {
            version_label_->set_text("Not installed");
        }

        install_btn_->set_visible(!installed);
        upgrade_btn_->set_visible(installed);
        uninstall_btn_->set_visible(installed);

        install_btn_->set_sensitive(has_file && !busy_);
        upgrade_btn_->set_sensitive(has_file && !busy_);
        uninstall_btn_->set_sensitive(!busy_);
    }

    void on_browse() {
        if (!filedialog_) filedialog_ = Gtk::FileDialog::create();
        filedialog_->set_title("Select the DaVinci Resolve installer");
        auto filt = Gtk::FileFilter::create();
        filt->set_name("DaVinci Resolve installer (*.run, *.AppImage)");
        filt->add_pattern("*.run");
        filt->add_pattern("*.AppImage");
        auto model = Gio::ListStore<Gtk::FileFilter>::create();
        model->append(filt);
        filedialog_->set_filters(model);
        filedialog_->open(*this, sigc::mem_fun(*this, &DavinciWindow::on_file_selected));
    }

    void on_file_selected(const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto f = filedialog_->open_finish(result);
            if (f) file_entry_->set_text(f->get_path());
        } catch (const Glib::Error&) {
            // cancelled
        }
    }

    void on_install() {
        if (busy_) return;
        auto path = file_entry_->get_text();
        if (path.empty()) {
            term_->log("Select the installer file first.\n");
            set_status("Select a .run/.AppImage file to begin");
            return;
        }
        busy_ = true;
        refresh();
        term_->log("Starting installation of DaVinci Resolve...\n");
        std::thread([this, path] { install_worker(path.raw()); }).detach();
    }

    void install_worker(const std::string& run_path) {
        post([this] { set_status("Extracting installer..."); });
        std::string appdir = extract_appimage(run_path);
        if (appdir.empty()) {
            post([this] {
                term_->log("Error: AppImage extraction failed.\n");
                set_status("Installation failed");
                busy_ = false;
                refresh();
            });
            return;
        }

        std::string miss = missing_deps_list();
        if (miss.empty())
            post([this] { term_->log("All dependencies satisfied.\n"); });
        else
            post([this, miss] { term_->log("Missing dependencies: " + miss + "\n"); });

        std::string dep_cmd = dep_install_cmd();
        post([this, has = !dep_cmd.empty()] {
            if (has) term_->log("Installing missing dependencies via pkexec...\n");
            set_status("Installing (enter password)...");
        });

        std::string error;
        bool ok = install_rooted(appdir, dep_cmd, error);

        std::string cleanup = "rm -rf " + sig::shq(appdir);
        sig::run_capture({"sh", "-c", cleanup});

        if (!ok) {
            post([this, error] {
                term_->log("Error: " + error + "\n");
                set_status("Installation failed");
                busy_ = false;
                refresh();
            });
            return;
        }

        std::string ver = get_version();
        save_state(ver, run_path);
        post([this, ver] {
            version_label_->set_text(ver.empty() ? "installed" : "v" + ver);
            set_status("Installation complete");
            term_->log("Installation complete. Launch Resolve from the app menu.\n");
            busy_ = false;
            refresh();
        });
    }

    void on_uninstall() {
        if (busy_) return;
        busy_ = true;
        refresh();
        term_->log("Uninstalling DaVinci Resolve...\n");
        std::thread([this] {
            std::string error;
            bool ok = uninstall_rooted(error);
            clear_state();
            post([this, ok, error] {
                if (ok) {
                    term_->log("Uninstalled successfully.\n");
                    set_status("Uninstalled");
                } else {
                    term_->log("Error: " + error + "\n");
                    set_status("Uninstall failed");
                }
                busy_ = false;
                refresh();
            });
        }).detach();
    }
};

int main(int argc, char** argv) {
    auto app = Gtk::Application::create("org.sigeonos.davinci");
    app->make_window_and_run<DavinciWindow>(argc, argv);
}