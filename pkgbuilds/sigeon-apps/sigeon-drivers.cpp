#include "common.hpp"
#include <thread>
#include <unistd.h>

static std::string lowercase(std::string s) {
    for (auto& c : s) c = std::tolower(c);
    return s;
}

static std::string detect_gpu() {
    std::string out = sig::run_capture({"sh", "-c",
        "lspci 2>/dev/null | grep -iE 'vga|3d|display' | head -1"});
    return sig::trim(out);
}

static bool is_pkg_installed(const std::string& pkg) {
    return std::system(("pacman -Qi " + pkg + " >/dev/null 2>&1").c_str()) == 0;
}

// Only decorate labels with a family when detection actually found one.
static std::string fam_suffix(const std::string& family) {
    return family.empty() || family == "Unknown" ? "" : " (" + family + ")";
}

struct GpuInfo {
    std::string family;    // e.g. "Turing", "Polaris (GCN 4)", "Unknown"
    bool supported;        // installable
    std::string plan;      // official repo packages (space separated) or empty
    std::string aur_pkgs;  // AUR packages to build (in build order) or empty
    std::string conf;      // kernel module options to write to /etc/modprobe.d/ (empty = none)
    std::string note;      // guidance for the user
};

// ArchWiki "Intel graphics": mesa is the recommended DRI driver (Gen 3 and
// later); the modesetting DDX is the recommended choice (xf86-video-intel is
// not recommended); vulkan-intel is supported from Broadwell (Gen 8) onwards;
// linux-firmware-intel provides the GuC/HuC firmware.
static GpuInfo intel_info(const std::string& gpu) {
    GpuInfo gi;
    gi.supported = true;
    std::string lower = lowercase(gpu);
    if      (lower.find("arc")           != std::string::npos ||
             lower.find("alchemist")     != std::string::npos ||
             lower.find("dg2")           != std::string::npos ||
             lower.find("battlemage")    != std::string::npos ||
             lower.find("bmg")           != std::string::npos ||
             lower.find("celestial")     != std::string::npos ||
             lower.find("iris xe")       != std::string::npos ||
             lower.find("xe graphics")   != std::string::npos) gi.family = "Gen 12+ (Xe)";
    else if (lower.find("hd graphics 9") != std::string::npos ||
             lower.find("uhd graphics 9") != std::string::npos) gi.family = "Gen 11 (Ice Lake)";
    else if (lower.find("hd graphics 7") != std::string::npos ||
             lower.find("uhd graphics 7") != std::string::npos) gi.family = "Gen 12";
    else if (lower.find("hd graphics 6") != std::string::npos ||
             lower.find("uhd graphics 6") != std::string::npos ||
             lower.find("iris plus")     != std::string::npos ||
             lower.find("iris 6")        != std::string::npos)  gi.family = "Gen 9-11";
    else if (lower.find("uhd")           != std::string::npos)  gi.family = "Gen 9-12";
    else if (lower.find("hd graphics 5") != std::string::npos ||
             lower.find("pro graphics 6") != std::string::npos) gi.family = "Gen 8 (Broadwell)";
    else if (lower.find("iris 5")        != std::string::npos ||
             lower.find("pro graphics 5") != std::string::npos ||
             lower.find("hd graphics 4") != std::string::npos)  gi.family = "Gen 7 (Haswell)";
    else if (lower.find("hd graphics 3") != std::string::npos)  gi.family = "Gen 6";
    else if (lower.find("hd graphics 2") != std::string::npos)  gi.family = "Gen 6";
    else if (lower.find("gma")           != std::string::npos)  gi.family = "GMA (Gen 3-5)";
    else gi.family = "Unknown";

    if (gi.family == "GMA (Gen 3-5)") {
        gi.plan = "mesa";
        gi.note = "GMA 3600 series (PowerVR) is not supported by open source drivers; "
                  "other GMA cards have limited support via the legacy i915 driver.";
    } else {
        bool vulkan = gi.family != "Gen 7 (Haswell)" && gi.family != "Gen 6";
        gi.plan = "mesa";
        if (vulkan) gi.plan += " vulkan-intel";
        gi.plan += " linux-firmware-intel intel-media-driver";
        if (std::system("grep -q '^\\[multilib\\]' /etc/pacman.conf 2>/dev/null") == 0)
            gi.plan += std::string(" lib32-mesa") + (vulkan ? " lib32-vulkan-intel" : "");
        gi.note = "mesa + modesetting is the recommended stack (xf86-video-intel is "
                  "not recommended).";
        if (!vulkan)
            gi.note += " Vulkan is not supported before Broadwell (Gen 8), so "
                       "vulkan-intel is skipped.";
    }
    return gi;
}

// ArchWiki "NVIDIA": determine the card family (nouveau code names page) and
// pick the driver: nvidia-open for Turing and newer, otherwise the legacy
// proprietary driver which is only available from the AUR.
static GpuInfo nvidia_info(const std::string& gpu) {
    GpuInfo ni;
    std::string lower = lowercase(gpu);
    if      (lower.find("rtx 50") != std::string::npos) ni.family = "Blackwell";
    else if (lower.find("rtx 40") != std::string::npos) ni.family = "Ada Lovelace";
    else if (lower.find("rtx 30") != std::string::npos) ni.family = "Ampere";
    else if (lower.find("rtx 20") != std::string::npos ||
             lower.find("gtx 16") != std::string::npos) ni.family = "Turing";
    else if (lower.find("gtx 10") != std::string::npos) ni.family = "Pascal";
    else if (lower.find("gtx 9")  != std::string::npos ||
             lower.find("gtx 75") != std::string::npos ||
             lower.find("gtx 74") != std::string::npos) ni.family = "Maxwell";
    else if (lower.find("titan v") != std::string::npos ||
             lower.find("v100")  != std::string::npos ||
             lower.find("gv100") != std::string::npos)  ni.family = "Volta";
    else if (lower.find("gtx 7")  != std::string::npos ||
             lower.find("gt 7")   != std::string::npos) ni.family = "Kepler";
    else if (lower.find("gtx 5")  != std::string::npos ||
             lower.find("gt 5")   != std::string::npos ||
             lower.find("gtx 4")  != std::string::npos ||
             lower.find("gt 4")   != std::string::npos) ni.family = "Fermi";
    else if (lower.find("8800")   != std::string::npos ||
             lower.find("gtx 2")  != std::string::npos) ni.family = "Tesla";
    else ni.family = "Unknown";

    ni.supported = ni.family == "Blackwell" || ni.family == "Ada Lovelace" ||
                   ni.family == "Ampere"    || ni.family == "Turing";
    if (ni.supported) {
        if (is_pkg_installed("linux"))          ni.plan += "nvidia-open ";
        else if (is_pkg_installed("linux-lts")) ni.plan += "nvidia-open-lts ";
        else                                    ni.plan += "nvidia-open-dkms linux-headers ";
        ni.plan += "nvidia-utils nvidia-settings ";
        if (std::system("grep -q '^\\[multilib\\]' /etc/pacman.conf 2>/dev/null") == 0)
            ni.plan += "lib32-nvidia-utils";
        ni.plan = sig::trim(ni.plan);
        ni.conf = "options nvidia_drm modeset=1 fbdev=1\n";
        ni.note = "nvidia-open is the recommended driver for this family.";
    } else if (ni.family == "Unknown") {
        ni.note = "Could not determine the NVIDIA card family. The open driver only supports "
                  "Turing and newer cards; older cards keep using nouveau.";
    } else {
        // Legacy families: the proprietary driver is only packaged in the AUR
        // (ArchWiki: nvidia-580xx-dkms for Volta/Pascal/Maxwell, nvidia-470xx-dkms
        // for Kepler, nvidia-390xx-dkms for Fermi, nvidia-340xx-dkms for Tesla).
        // Build order matters: the -utils package must exist before -dkms.
        if (ni.family == "Volta" || ni.family == "Pascal" || ni.family == "Maxwell") {
            ni.aur_pkgs = "nvidia-580xx-utils nvidia-580xx-dkms";
            ni.conf = "options nvidia_drm modeset=1 fbdev=1\n";
            if (std::system("grep -q '^\\[multilib\\]' /etc/pacman.conf 2>/dev/null") == 0)
                ni.aur_pkgs += " lib32-nvidia-580xx-utils";
        } else if (ni.family == "Kepler") {
            ni.aur_pkgs = "nvidia-470xx-utils nvidia-470xx-dkms";
            ni.conf = "options nvidia_drm modeset=1\n";
            if (std::system("grep -q '^\\[multilib\\]' /etc/pacman.conf 2>/dev/null") == 0)
                ni.aur_pkgs += " lib32-nvidia-470xx-utils";
        } else if (ni.family == "Fermi") {
            ni.aur_pkgs = "nvidia-390xx-utils nvidia-390xx-dkms";
            ni.conf = "options nvidia_drm modeset=1\n";
            if (std::system("grep -q '^\\[multilib\\]' /etc/pacman.conf 2>/dev/null") == 0)
                ni.aur_pkgs += " lib32-nvidia-390xx-utils";
        } else {
            ni.aur_pkgs = "nvidia-340xx-utils nvidia-340xx-dkms";
            ni.conf = "options nvidia_drm modeset=1\n";
        }
        ni.note = ni.family + " cards are served by the legacy proprietary driver, which "
                  "is only available from the AUR. The app will build it on this machine "
                  "(several minutes) - reboot afterwards.";
    }
    return ni;
}

// ArchWiki "AMDGPU": amdgpu covers GCN 1 (2012) and later. All supported
// families share the same open-source stack: mesa + vulkan-radeon, with the
// modesetting DDX as the recommended choice (xf86-video-amdgpu is only needed
// for TearFree or hardware-specific issues). Pre-GCN cards keep the radeon
// driver.
static GpuInfo amd_info(const std::string& gpu) {
    GpuInfo gi;
    gi.supported = true;
    std::string lower = lowercase(gpu);
    if      (lower.find("rx 9")    != std::string::npos) gi.family = "RDNA 4";
    else if (lower.find("rx 7")    != std::string::npos) gi.family = "RDNA 3";
    else if (lower.find("rx 6")    != std::string::npos) gi.family = "RDNA 2";
    else if (lower.find("rx 5700") != std::string::npos ||
             lower.find("rx 5600") != std::string::npos ||
             lower.find("rx 5500") != std::string::npos) gi.family = "RDNA 1";
    else if (lower.find("rx vega") != std::string::npos ||
             lower.find("radeon vii") != std::string::npos ||
             lower.find("vega")    != std::string::npos) gi.family = "Vega (GCN 5)";
    else if (lower.find("rx 5")    != std::string::npos ||
             lower.find("rx 4")    != std::string::npos) gi.family = "Polaris (GCN 4)";
    else if (lower.find("fury")    != std::string::npos ||
             lower.find("r9 3")    != std::string::npos) gi.family = "GCN 3";
    else if (lower.find("r9 2")    != std::string::npos ||
             lower.find("r7 2")    != std::string::npos ||
             lower.find("r5 2")    != std::string::npos) gi.family = "GCN 1-2";
    else if (lower.find("hd 8")    != std::string::npos ||
             lower.find("hd 7")    != std::string::npos) gi.family = "GCN 1-2";
    else if (lower.find("hd 6")    != std::string::npos ||
             lower.find("hd 5")    != std::string::npos ||
             lower.find("hd 4")    != std::string::npos ||
             lower.find("hd 3")    != std::string::npos ||
             lower.find("hd 2")    != std::string::npos) gi.family = "pre-GCN";
    else gi.family = "Unknown";

    if (gi.family == "pre-GCN") {
        gi.plan = "mesa";
        gi.note = "This card predates GCN 1 - amdgpu does not support it; the radeon "
                  "driver keeps providing the display.";
    } else {
        gi.plan = "mesa vulkan-radeon";
        if (std::system("grep -q '^\\[multilib\\]' /etc/pacman.conf 2>/dev/null") == 0)
            gi.plan += " lib32-mesa lib32-vulkan-radeon";
        gi.note = gi.family == "Unknown"
            ? "Assuming GCN 1 (2012) or newer - amdgpu + mesa is the recommended stack "
              "(modesetting DDX)."
            : "amdgpu + mesa is the recommended stack (modesetting DDX; "
              "xf86-video-amdgpu is only needed for TearFree).";
    }
    return gi;
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
        auto gpu = detect_gpu();
        auto low = lowercase(gpu);
        bool is_nv = low.find("nvidia") != std::string::npos;
        bool is_amd = low.find("amd") != std::string::npos ||
                      low.find("ati") != std::string::npos ||
                      low.find("radeon") != std::string::npos;
        bool is_intel = low.find("intel") != std::string::npos;
        auto nv = nvidia_info(gpu);
        auto am = amd_info(gpu);
        auto ii = intel_info(gpu);
        gb->append(*make_drv_btn("Install NVIDIA" + fam_suffix(nv.family), nv, is_nv, "NVIDIA"));
        gb->append(*make_drv_btn("Install AMD" + fam_suffix(am.family), am, is_amd, "AMD"));
        gb->append(*make_drv_btn("Install Intel" + fam_suffix(ii.family), ii, is_intel, "Intel"));
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
        auto kern = sig::run_capture({"sh", "-c",
            "lspci -k -d ::03xx 2>/dev/null | grep -i 'kernel driver' | head -1"});
        std::string fam;
        std::string gpu_low = lowercase(info);
        if (gpu_low.find("nvidia") != std::string::npos)      fam = fam_suffix(nvidia_info(info).family);
        else if (gpu_low.find("amd") != std::string::npos ||
                 gpu_low.find("ati") != std::string::npos ||
                 gpu_low.find("radeon") != std::string::npos) fam = fam_suffix(amd_info(info).family);
        else if (gpu_low.find("intel") != std::string::npos)  fam = fam_suffix(intel_info(info).family);
        info_->set_markup("<b>Detected GPU:</b> " + Glib::ustring(info.empty() ? "unknown" : info) + fam +
                          "\n<b>Kernel driver:</b> " + Glib::ustring(kern.empty() ? "none" : kern));
    }

private:
    Gtk::Label* info_ = nullptr;
    Gtk::TextView* output_view_ = nullptr;
    std::shared_ptr<Term> term_;
    bool busy_ = false;

    Gtk::Button* make_btn(const Glib::ustring& label, const Glib::ustring& pkgs) {
        auto* b = Gtk::make_managed<Gtk::Button>(label);
        auto pkgs_copy = std::make_shared<Glib::ustring>(pkgs);
        b->signal_clicked().connect([this, pkgs_copy] { install(*pkgs_copy, ""); });
        return b;
    }

    Gtk::Button* make_drv_btn(const Glib::ustring& label, const GpuInfo& gi,
                              bool detected, const char* vendor) {
        auto* b = Gtk::make_managed<Gtk::Button>(label);
        auto info = std::make_shared<GpuInfo>(gi);
        b->signal_clicked().connect([this, info, detected, vendor] {
            if (detected && info->family != "Unknown")
                term_->log("\n==> Detected family: " + info->family + ".\n");
            else if (detected)
                term_->log("\n==> GPU detected but the model was not recognized - installing "
                           "the standard " + std::string(vendor) + " stack anyway.\n");
            else
                term_->log("\n==> No " + std::string(vendor) + " GPU detected - installing "
                           "the standard " + std::string(vendor) + " stack anyway.\n");
            term_->log("==> " + info->note + "\n");
            if (!info->plan.empty()) install(info->plan, info->conf);
            else if (!info->aur_pkgs.empty()) install_aur(info->aur_pkgs, info->conf);
        });
        return b;
    }

    void post(const std::function<void()>& fn) {
        Glib::MainContext::get_default()->invoke([fn] { fn(); return false; });
    }

    void install(const Glib::ustring& pkgs, const std::string& conf) {
        if (busy_) return;
        busy_ = true;
        term_->log("\n==> Installing: " + pkgs + "\n");
        std::thread([this, pkgs, conf] {
            // The live medium ships without pacman sync DBs, so refresh them
            // before looking up/installing any package.
            auto stream_out = [this](const char* line) {
                std::string s(line);
                post([this, s] { term_->log(s); });
            };
            term_->log("\n==> Refreshing package lists...\n");
            int syn = sig::run_stream({"pkexec", "pacman", "-Sy", "--noconfirm"}, stream_out);
            if (syn != 0) {
                post([this, syn] {
                    term_->log("\n==> Could not refresh package lists (exit code " +
                               std::to_string(syn) + ").\n");
                    busy_ = false;
                });
                return;
            }
            std::vector<std::string> args = {"pkexec", "pacman", "-S", "--noconfirm", "--needed"};
            // split pkgs on spaces
            std::string cur;
            for (char c : pkgs.raw()) {
                if (c == ' ') { if (!cur.empty()) { args.push_back(cur); cur.clear(); } }
                else cur += c;
            }
            if (!cur.empty()) args.push_back(cur);
            int status = sig::run_stream(args, stream_out);
            if (status == 0 && !conf.empty()) setup_conf(conf, stream_out);
            post([this, status] {
                term_->log(status == 0
                    ? "\n==> Done. Reboot for the driver to take effect.\n"
                    : "\n==> Installation failed (exit code " + std::to_string(status) + ").\n");
                busy_ = false;
            });
        }).detach();
    }

    // Write kernel module options (e.g. nvidia_drm KMS settings) via pkexec.
    void setup_conf(const std::string& conf,
                    const std::function<void(const char*)>& stream_out) {
        post([this] {
            term_->log("\n==> Writing kernel options to /etc/modprobe.d/nvidia.conf...\n");
        });
        FILE* f = fopen("/tmp/sigeon-nvidia.conf", "w");
        if (!f) {
            post([this] { term_->log("==> Could not write the config file.\n"); });
            return;
        }
        fputs(conf.c_str(), f);
        fclose(f);
        int rc = sig::run_stream({"sh", "-c",
            "pkexec sh -c 'install -m644 /tmp/sigeon-nvidia.conf /etc/modprobe.d/nvidia.conf "
            "&& rm -f /tmp/sigeon-nvidia.conf'"}, stream_out);
        post([this, rc] {
            term_->log(rc == 0
                ? "==> Config written - it takes effect after reboot.\n"
                : "==> Could not write the config (exit code " + std::to_string(rc) + ").\n");
        });
    }

    // Legacy drivers (e.g. pre-Turing NVIDIA) exist only in the AUR. Build them
    // on this machine: official repo build deps first, then clone + makepkg each
    // AUR package (in the given build order) and install it with pkexec.
    void install_aur(const std::string& aur, const std::string& conf) {
        if (busy_) return;
        busy_ = true;
        term_->log("\n==> Installing from AUR: " + aur + "\n");
        std::thread([this, aur, conf] {
            if (geteuid() == 0) {
                post([this] {
                    term_->log("\n==> AUR builds cannot run in the root live session.\n"
                               "Install Sigeon OS first, then run Drivers to build the "
                               "legacy driver.\n");
                    busy_ = false;
                });
                return;
            }
            auto stream_out = [this](const char* line) {
                std::string s(line);
                post([this, s] { term_->log(s); });
            };
            std::vector<std::string> deps = {"pkexec", "pacman", "-Sy", "--noconfirm",
                                             "base-devel", "git", "dkms", "linux-headers"};
            if (std::system("grep -q '^\\[multilib\\]' /etc/pacman.conf 2>/dev/null") == 0)
                deps.push_back("lib32-gcc-libs");
            int status = sig::run_stream(deps, stream_out);
            if (status != 0) {
                post([this, status] {
                    term_->log("\n==> Could not install build dependencies (exit code " +
                               std::to_string(status) + ").\n");
                    busy_ = false;
                });
                return;
            }
            std::string cur;
            for (char c : aur) {
                if (c == ' ') {
                    if (!cur.empty()) { if (!build_aur_pkg(cur, stream_out)) return; cur.clear(); }
                } else cur += c;
            }
            if (!cur.empty() && !build_aur_pkg(cur, stream_out)) return;
            if (!conf.empty()) setup_conf(conf, stream_out);
            post([this] {
                term_->log("\n==> Done. Reboot for the driver to take effect.\n");
                busy_ = false;
            });
        }).detach();
    }

    bool build_aur_pkg(const std::string& pkg,
                       const std::function<void(const char*)>& stream_out) {
        std::string dir = "/tmp/sigeon-aur-" + pkg;
        post([this, pkg] { term_->log("\n==> Building " + pkg + " from AUR...\n"); });
        int status = sig::run_stream({"sh", "-c",
            "rm -rf '" + dir + "' && git clone -q https://aur.archlinux.org/" + pkg + ".git '" + dir + "'"},
            stream_out);
        if (status != 0) {
            post([this, pkg, status] {
                term_->log("\n==> Could not fetch " + pkg + " from the AUR (exit code " +
                           std::to_string(status) + ").\n");
                busy_ = false;
            });
            return false;
        }
        status = sig::run_stream({"sh", "-c",
            "cd '" + dir + "' && makepkg -f --noconfirm"}, stream_out);
        if (status != 0) {
            post([this, pkg, status] {
                term_->log("\n==> Building " + pkg + " failed (exit code " +
                           std::to_string(status) + ").\n");
                busy_ = false;
            });
            return false;
        }
        status = sig::run_stream({"sh", "-c",
            "pkexec pacman -U --noconfirm '" + dir + "'/*.pkg.tar.zst"}, stream_out);
        if (status != 0) {
            post([this, pkg, status] {
                term_->log("\n==> Installing " + pkg + " failed (exit code " +
                           std::to_string(status) + ").\n");
                busy_ = false;
            });
            return false;
        }
        return true;
    }
};

int main(int argc, char** argv) {
    auto app = Gtk::Application::create("org.sigeonos.drivers");
    app->make_window_and_run<DriversWindow>(argc, argv);
}