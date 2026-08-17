#include "common.hpp"
#include <thread>
#include <filesystem>
#include <fstream>

// Build a wide, CachyOS-Hello-style "quick action" card button:
// an icon name, a bold title and a subtitle, laid out vertically.
static Gtk::Button* make_card(const Glib::ustring& icon,
                              const Glib::ustring& title,
                              const Glib::ustring& sub) {
    auto* btn = Gtk::make_managed<Gtk::Button>();
    btn->set_hexpand(true);
    btn->set_vexpand(true);

    auto* vb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    vb->set_margin_top(6);
    vb->set_margin_bottom(6);
    vb->set_halign(Gtk::Align::START);

    auto* iconl = Gtk::make_managed<Gtk::Image>();
    iconl->set_from_icon_name(icon);
    iconl->set_pixel_size(40);
    iconl->set_halign(Gtk::Align::START);

    auto* tit = Gtk::make_managed<Gtk::Label>();
    tit->set_markup("<span weight='bold' size='large'>" + Glib::Markup::escape_text(title) + "</span>");
    tit->set_xalign(0);
    tit->set_halign(Gtk::Align::START);

    auto* subl = Gtk::make_managed<Gtk::Label>();
    subl->set_markup("<span size='small'>" + Glib::Markup::escape_text(sub) + "</span>");
    subl->set_xalign(0);
    subl->set_halign(Gtk::Align::START);
    subl->set_wrap(true);

    vb->append(*iconl);
    vb->append(*tit);
    vb->append(*subl);
    btn->set_child(*vb);
    return btn;
}

static void open_app(const Glib::ustring& argv0) {
    try {
        Glib::spawn_command_line_async(argv0);
    } catch (const Glib::Error& e) {
        std::fprintf(stderr, "sigeon-hello: could not launch %s: %s\n",
                     argv0.c_str(), e.what());
    }
}

// User-level autostart override for the Welcome window.
static std::string autostart_path() {
    std::string home = getenv("HOME") ? getenv("HOME") : "/root";
    return home + "/.config/autostart/sigeon-hello.desktop";
}

static bool autostart_enabled() {
    try {
        std::ifstream f(autostart_path());
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("Hidden=true") != std::string::npos) return false;
        }
    } catch (...) {}
    return true; // system autostart (/etc/xdg/autostart) is on by default
}

static void set_autostart(bool enable) {
    const std::string path = autostart_path();
    try {
        if (enable) {
            std::filesystem::remove(path);
        } else {
            std::filesystem::create_directories(
                std::filesystem::path(path).parent_path());
            std::ofstream f(path);
            f << "[Desktop Entry]\n"
                 "Type=Application\n"
                 "Name=Sigeon Hello\n"
                 "Exec=sigeon-hello\n"
                 "Icon=sigeon-hello\n"
                 "Terminal=false\n"
                 "Hidden=true\n";
        }
    } catch (const std::exception&) {}
}

class HelloWindow : public Gtk::Window {
public:
    HelloWindow() {
        set_title("Sigeon OS - Welcome");
        set_default_size(760, 500);

        auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

        auto* banner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
        banner->set_margin_top(28);
        banner->set_margin_bottom(16);
        banner->set_margin_start(28);
        banner->set_margin_end(28);

        auto* logo = Gtk::make_managed<Gtk::Image>();
        logo->set_from_icon_name("sigeon-hello");
        logo->set_pixel_size(64);

        auto* titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        auto* t1 = Gtk::make_managed<Gtk::Label>();
        t1->set_markup("<span size='xx-large' weight='bold'>Sigeon OS</span>");
        t1->set_xalign(0);
        auto* t2 = Gtk::make_managed<Gtk::Label>(
            "Welcome! Pick a task below to get started.");
        t2->set_xalign(0);
        t2->set_selectable(true);
        titles->append(*t1);
        titles->append(*t2);

        banner->append(*logo);
        banner->append(*titles);

        auto* grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_row_spacing(12);
        grid->set_column_spacing(12);
        grid->set_row_homogeneous(false);
        grid->set_hexpand(true);
        grid->set_margin_start(28);
        grid->set_margin_end(28);
        grid->set_margin_bottom(28);

        // Column 1
        auto* up = make_card("emblem-important",
            "System Update", "Check for and install system updates");
        up->signal_clicked().connect([] { open_app("sigeon-updater"); });

        auto* drv = make_card("preferences-system",
            "Drivers", "Install GPU, audio and printer drivers");
        drv->signal_clicked().connect([] { open_app("sigeon-drivers"); });

        // Column 2
        auto* term = make_card("utilities-terminal",
            "Open Terminal", "Access the command line");
        term->signal_clicked().connect([] { open_app("xfce4-terminal"); });

        auto* dav = make_card("media-playback-start",
            "DaVinci Resolve", "Install or update DaVinci Resolve");
        dav->signal_clicked().connect([] { open_app("sigeon-davinci"); });

        auto* store = make_card("system-software-install",
            "Sigeon Store", "Browse and install apps from Flathub");
        store->signal_clicked().connect([] { open_app("sigeon-store"); });

        grid->attach(*up, 0, 0, 1, 1);
        grid->attach(*term, 1, 0, 1, 1);
        grid->attach(*drv, 0, 1, 1, 1);
        grid->attach(*store, 1, 1, 1, 1);
        grid->attach(*dav, 0, 2, 1, 1);

        root->append(*banner);
        root->append(*grid);

        auto* bottom = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        bottom->set_margin_start(28);
        bottom->set_margin_end(28);
        bottom->set_margin_bottom(20);

        auto* toggle = Gtk::make_managed<Gtk::Switch>();
        toggle->set_active(autostart_enabled());
        toggle->set_valign(Gtk::Align::CENTER);
        toggle->signal_state_set().connect(sigc::slot<bool(bool)>([](bool on) -> bool {
            set_autostart(on);
            return false;
        }), false);

        auto* tlabel = Gtk::make_managed<Gtk::Label>();
        tlabel->set_markup("Launch Sigeon Hello on startup");
        tlabel->set_xalign(0);
        tlabel->set_hexpand(true);

        bottom->append(*tlabel);
        bottom->append(*toggle);
        root->append(*bottom);

        set_child(*root);
    }
};

int main(int argc, char** argv) {
    auto app = Gtk::Application::create("org.sigeonos.hello");
    app->make_window_and_run<HelloWindow>(argc, argv);
}