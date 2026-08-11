#include "common.hpp"
#include <regex>
#include <thread>

static std::string pacman_cmd() {
    return "pacman";
}

// Parse Arch Linux news RSS: returns vector of {title, date-string}.
static std::vector<std::pair<std::string, std::string>> parse_arch_news(const std::string& xml) {
    std::vector<std::pair<std::string, std::string>> items;
    std::regex re("<item>([\\s\\S]*?)</item>", std::regex::icase);
    std::regex title_re("<title>\\s*(?:<!\\[CDATA\\[)?([\\s\\S]*?)(?:\\]\\]>)?\\s*</title>", std::regex::icase);
    std::regex date_re("<pubDate>\\s*([\\s\\S]*?)\\s*</pubDate>", std::regex::icase);
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        std::string item = it->str(1);
        std::string title, date;
        std::smatch m;
        if (std::regex_search(item, m, title_re)) title = sig::trim(m[1].str());
        if (std::regex_search(item, m, date_re)) date = sig::trim(m[1].str());
        if (!title.empty()) items.emplace_back(title, date);
    }
    return items;
}

class UpdaterWindow : public Gtk::Window {
public:
    UpdaterWindow() {
        set_title("Sigeon Updater");
        set_default_size(720, 640);

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        box->set_margin(16);

        news_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        news_frame_ = Gtk::make_managed<Gtk::Frame>("Arch Linux news");
        news_frame_->set_child(*news_box_);
        news_frame_->set_visible(false);

        updates_view_ = Gtk::make_managed<Gtk::TextView>();
        updates_view_->set_editable(false);
        updates_view_->set_monospace(true);
        updates_view_->set_wrap_mode(Gtk::WrapMode::WORD);
        updates_view_->set_size_request(-1, 140);
        auto* uscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        uscroll->set_child(*updates_view_);
        auto* uframe = Gtk::make_managed<Gtk::Frame>("Available updates");
        uframe->set_child(*uscroll);

        output_view_ = Gtk::make_managed<Gtk::TextView>();
        output_view_->set_editable(false);
        output_view_->set_monospace(true);
        output_view_->set_wrap_mode(Gtk::WrapMode::CHAR);
        auto* oscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        oscroll->set_vexpand(true);
        oscroll->set_child(*output_view_);
        auto* oframe = Gtk::make_managed<Gtk::Frame>("Log");
        oframe->set_child(*oscroll);

        term_ = std::make_shared<Term>(*output_view_);

        auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        check_btn_ = Gtk::make_managed<Gtk::Button>("Check for updates");
        check_btn_->signal_clicked().connect(sigc::mem_fun(*this, &UpdaterWindow::on_check));
        update_btn_ = Gtk::make_managed<Gtk::Button>("Update system");
        update_btn_->get_style_context()->add_class("suggested-action");
        update_btn_->set_sensitive(false);
        update_btn_->signal_clicked().connect(sigc::mem_fun(*this, &UpdaterWindow::on_update));
        news_btn_ = Gtk::make_managed<Gtk::ToggleButton>("Show Arch news");
        news_btn_->signal_toggled().connect([this] {
            news_frame_->set_visible(news_btn_->get_active());
        });
        buttons->append(*check_btn_);
        buttons->append(*update_btn_);
        buttons->append(*news_btn_);

        box->append(*news_frame_);
        box->append(*uframe);
        box->append(*oframe);
        box->append(*buttons);
        set_child(*box);
    }

private:
    Gtk::Frame* news_frame_ = nullptr;
    Gtk::Box* news_box_ = nullptr;
    Gtk::TextView* updates_view_ = nullptr;
    Gtk::TextView* output_view_ = nullptr;
    Gtk::Button* check_btn_ = nullptr;
    Gtk::Button* update_btn_ = nullptr;
    Gtk::ToggleButton* news_btn_ = nullptr;
    std::shared_ptr<Term> term_;
    bool busy_ = false;

    void post(const std::function<void()>& fn) {
        Glib::MainContext::get_default()->invoke([fn] { fn(); return false; });
    }

    void on_check() {
        if (busy_) return;
        busy_ = true;
        check_btn_->set_sensitive(false);
        term_->log("Refreshing package lists...\n");
        std::thread([this] {
            auto sync = sig::run_capture({pacman_cmd(), "-Sy", "--noconfirm"});
            auto outdated = sig::run_capture({pacman_cmd(), "-Qu"});
            auto news_xml = sig::run_capture({"curl", "-s", "--max-time", "15",
                                              "https://archlinux.org/feeds/news/"});
            post([this, sync, outdated, news_xml] {
                auto buf = updates_view_->get_buffer();
                buf->set_text(outdated.empty() ? "System is up to date." : outdated);
                term_->log(sync);
                term_->log(outdated.empty() ? "\n=> System is up to date.\n" : "\n=> Updates found.\n");
                load_news(news_xml);
                bool any = !sig::trim(outdated).empty();
                update_btn_->set_sensitive(any);
                busy_ = false;
                check_btn_->set_sensitive(true);
            });
        }).detach();
    }

    void load_news(const std::string& xml) {
        auto items = parse_arch_news(xml);
        // clear news box children
        auto kids = news_box_->get_children();
        for (auto* k : kids) news_box_->remove(*k);

        if (items.empty()) {
            auto* l = Gtk::make_managed<Gtk::Label>("Could not fetch Arch news (offline?).");
            l->set_xalign(0);
            news_box_->append(*l);
            return;
        }

        bool manual = false;
        auto* warn = Gtk::make_managed<Gtk::Label>();
        warn->set_wrap(true);
        warn->set_xalign(0);
        warn->set_selectable(true);

        Glib::ustring text;
        for (size_t i = 0; i < items.size() && i < 6; i++) {
            const auto& [t, d] = items[i];
            std::string ago = sig::run_capture({"date", "-d", d, "+%Y-%m-%d"});
            text += (i == 0 ? "LATEST" : "-------") + Glib::ustring(" ") + t + "\n";
            if (i == 0) {
                // If the newest item is recent, warn before updating.
                std::string days = sig::run_capture({"sh", "-c",
                    "a=$(date -d " + sig::shq(d) + " +%s 2>/dev/null); b=$(date +%s); "
                    "echo $(( (b - a) / 86400 ))"});
                int ndays = std::atoi(sig::trim(days).c_str());
                if (ndays >= 0 && ndays <= 21) {
                    manual = true;
                    warn->set_markup("<b>New Arch news post (" + Glib::ustring(std::to_string(ndays)) +
                                     " days ago). Review it before updating, some updates need manual steps.</b>");
                } else {
                    warn->set_text("No very recent Arch news. The update should be straightforward.");
                }
            }
        }
        warn->set_visible(manual || true);
        news_box_->append(*warn);
        auto* l = Gtk::make_managed<Gtk::Label>(text);
        l->set_xalign(0);
        l->set_selectable(true);
        l->set_wrap(true);
        news_box_->append(*l);
    }

    void on_update() {
        if (busy_) return;
        busy_ = true;
        update_btn_->set_sensitive(false);
        check_btn_->set_sensitive(false);
        term_->log("\n==> Running: pacman -Syu --noconfirm\n");
        std::thread([this] {
            auto out = sig::run_capture({pacman_cmd(), "-Syu", "--noconfirm"});
            post([this, out] {
                term_->log(out);
                term_->log("\n==> Update finished.\n");
                busy_ = false;
                check_btn_->set_sensitive(true);
            });
        }).detach();
    }
};

int main(int argc, char** argv) {
    if (geteuid() != 0)
        execl("/usr/bin/pkexec", "pkexec", argv[0], (char*)nullptr);
    auto app = Gtk::Application::create("org.sigeonos.updater");
    app->make_window_and_run<UpdaterWindow>(argc, argv);
}