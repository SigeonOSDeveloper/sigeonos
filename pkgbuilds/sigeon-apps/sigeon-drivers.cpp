#include "common.hpp"
#include <thread>

static std::string lowercase(std::string s) {
    for (auto& c : s) c = std::tolower(c);
    return s;
}

static std::string detect_gpu() {
    std::string out = sig::run_capture({"sh", "-c",
        "lspci 2>/dev/null | grep -iE 'vga|3d|display' | head -1"});
    return sig::trim(out);
}

class DriversWindow : public Gtk::Window {
public:
    DriversWindow() {
        set_title("Sigeon Drivers");
        set_default_size(640, 760);

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        box->set_margin(16);

        // Detected hardware
        info_ = Gtk::make_managed<Gtk::Label>();
        info_->set_wrap(true);
        info_->set_xalign(0);
        info_->set_selectable(true);

        auto* gpuframe = Gtk::make_managed<Gtk::Frame>("Graphics driver");
        auto* gpurow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        gpurow->set_margin(10);
        auto* gb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        gb->append(*make_btn("Install NVIDIA (open)", "nvidia-open nvidia-utils nvidia-settings"));
        gb->append(*make_btn("AMD (open)", "mesa xf86-video-amdgpu libva-mesa-driver"));
        gb->append(*make_btn("Intel", "mesa xf86-video-intel intel-media-driver"));
        gpurow->append(*gb);
        gpurow->append(*make_btn("Generic / Mesa fallback", "mesa"));
        gpuframe->set_child(*gpurow);

        auto* cpuframe = Gtk::make_managed<Gtk::Frame>("CPU microcode");
        auto* cpubox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        cpubox->set_margin(10);
        cpubox->append(*make_btn("Intel microcode", "intel-ucode"));
        cpubox->append(*make_btn("AMD microcode", "amd-ucode"));
        cpuframe->set_child(*cpubox);

        auto* audframe = Gtk::make_managed<Gtk::Frame>("Audio");
        auto* audbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        audbox->set_margin(10);
        audbox->append(*make_btn("PipeWire (recommended)", "pipewire pipewire-audio wireplumber pavucontrol"));
        audbox->append(*make_btn("ALSA tools", "alsa-utils alsa-firmware"));
        audframe->set_child(*audbox);

        auto* priframe = Gtk::make_managed<Gtk::Frame>("Printers");
        auto* pribox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        pribox->set_margin(10);
        pribox->append(*make_btn("CUPS + printing tools", "cups cups-pdf system-config-printer"));
        priframe->set_child(*pribox);

        output_view_ = Gtk::make_managed<Gtk::TextView>();
        output_view_->set_editable(false);
        output_view_->set_monospace(true);
        output_view_->set_wrap_mode(Gtk::WrapMode::CHAR);
        output_view_->set_vexpand(true);
        auto* oscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        oscroll->set_child(*output_view_);
        auto* oframe = Gtk::make_managed<Gtk::Frame>("Log");
        oframe->set_child(*oscroll);

        term_ = std::make_shared<Term>(*output_view_);

        box->append(*info_);
        box->append(*gpuframe);
        box->append(*cpuframe);
        box->append(*audframe);
        box->append(*priframe);
        box->append(*oframe);
        set_child(*box);

        auto info = detect_gpu();
        info_->set_markup("<b>Detected GPU:</b> " + Glib::ustring(info.empty() ? "unknown" : info));
    }

private:
    Gtk::Label* info_ = nullptr;
    Gtk::TextView* output_view_ = nullptr;
    std::shared_ptr<Term> term_;
    bool busy_ = false;

    Gtk::Button* make_btn(const Glib::ustring& label, const Glib::ustring& pkgs) {
        auto* b = Gtk::make_managed<Gtk::Button>(label);
        auto pkgs_copy = std::make_shared<Glib::ustring>(pkgs);
        b->signal_clicked().connect([this, pkgs_copy] { install(*pkgs_copy); });
        return b;
    }

    void post(const std::function<void()>& fn) {
        Glib::MainContext::get_default()->invoke([fn] { fn(); return false; });
    }

    void install(const Glib::ustring& pkgs) {
        if (busy_) return;
        busy_ = true;
        term_->log("\n==> Installing: " + pkgs + "\n");
        std::thread([this, pkgs] {
            // The live medium ships without pacman sync DBs, so refresh them
            // before looking up/installing any package.
            auto syn = sig::run_capture({"pacman", "-Sy", "--noconfirm"});
            std::vector<std::string> args = {"pacman", "-S", "--noconfirm", "--needed"};
            // split pkgs on spaces
            std::string cur;
            for (char c : pkgs.raw()) {
                if (c == ' ') { if (!cur.empty()) { args.push_back(cur); cur.clear(); } }
                else cur += c;
            }
            if (!cur.empty()) args.push_back(cur);
            auto out = sig::run_capture(args);
            post([this, syn, out] {
                term_->log(syn);
                term_->log(out);
                term_->log("\n==> Done.\n");
                busy_ = false;
            });
        }).detach();
    }
};

int main(int argc, char** argv) {
    if (geteuid() != 0)
        execl("/usr/bin/pkexec", "pkexec", argv[0], (char*)nullptr);
    auto app = Gtk::Application::create("org.sigeonos.drivers");
    app->make_window_and_run<DriversWindow>(argc, argv);
}