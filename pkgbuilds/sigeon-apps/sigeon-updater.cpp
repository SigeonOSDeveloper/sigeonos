#include "common.hpp"
#include <thread>
#include <chrono>

static std::vector<std::string> pacman_cmd() {
    return {"pkexec", "pacman"};
}

class UpdaterWindow : public Gtk::Window {
public:
    UpdaterWindow() {
        set_title("Sigeon Updater");
        set_default_size(720, 640);

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        box->set_margin(16);

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

        status_label_ = Gtk::make_managed<Gtk::Label>();
        status_label_->set_xalign(0);
        status_label_->set_wrap(true);

        auto* manual_note = Gtk::make_managed<Gtk::Label>();
        manual_note->set_xalign(0);
        manual_note->set_wrap(true);
        manual_note->set_selectable(true);
        manual_note->set_markup(
            "<b>Important:</b> This tool does not manage manual update steps. "
            "To check whether an update needs manual intervention, visit "
            "<a href=\"https://archlinux.org/news/\">archlinux.org/news/</a> before updating.");

        auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        check_btn_ = Gtk::make_managed<Gtk::Button>("Check for updates");
        check_btn_->signal_clicked().connect(sigc::mem_fun(*this, &UpdaterWindow::on_check));
        update_btn_ = Gtk::make_managed<Gtk::Button>("Update system");
        update_btn_->get_style_context()->add_class("suggested-action");
        update_btn_->set_sensitive(false);
        update_btn_->signal_clicked().connect(sigc::mem_fun(*this, &UpdaterWindow::on_update));
        buttons->append(*check_btn_);
        buttons->append(*update_btn_);

        box->append(*uframe);
        box->append(*oframe);
        box->append(*status_label_);
        box->append(*manual_note);
        box->append(*buttons);
        set_child(*box);
    }

private:
    Gtk::TextView* updates_view_ = nullptr;
    Gtk::TextView* output_view_ = nullptr;
    Gtk::Button* check_btn_ = nullptr;
    Gtk::Button* update_btn_ = nullptr;
    Gtk::Label* status_label_ = nullptr;
    std::shared_ptr<Term> term_;
    bool busy_ = false;

    void post(const std::function<void()>& fn) {
        Glib::MainContext::get_default()->invoke([fn] { fn(); return false; });
    }

    static bool db_locked() {
        return access("/var/lib/pacman/db.lck", F_OK) == 0;
    }

    static std::string available_updates() {
        return sig::run_capture({"pacman", "-Qu"});
    }

    static bool needs_reboot() {
        auto inst = sig::trim(sig::run_capture({"pacman", "-Qq", "linux"}));
        auto kern = sig::trim(sig::run_capture({"uname", "-r"}));
        if (inst.empty() || kern.empty()) return false;
        std::string a = inst, b = kern;
        auto dot = a.find('.');
        if (dot != std::string::npos) a = a.substr(0, dot);
        if (a != b.substr(0, a.size())) return true;
        return false;
    }

    void on_check() {
        if (busy_) return;
        busy_ = true;
        check_btn_->set_sensitive(false);
        update_btn_->set_sensitive(false);
        status_label_->set_text("");
        term_->log("Refreshing package lists...\n");
        std::thread([this] {
            if (db_locked()) {
                post([this] {
                    term_->log("\n(pacman database is locked - close other package managers first)\n");
                    term_->log("\n=> Failed to refresh package databases.\n");
                    busy_ = false;
                    check_btn_->set_sensitive(true);
                });
                return;
            }
            int sync_status = -1;
            for (int attempt = 1; attempt <= 3; attempt++) {
                std::vector<std::string> args = pacman_cmd();
                args.insert(args.end(), {"-Sy", "--noconfirm", "--disable-download-timeout"});
                sync_status = sig::run_stream(args, [this](const char* line) {
                    std::string s(line);
                    post([this, s] { term_->log(s); });
                });
                if (sync_status == 0) break;
                std::this_thread::sleep_for(std::chrono::seconds(2 * attempt));
            }
            auto outdated = sync_status == 0 ? available_updates() : "";
            post([this, sync_status, outdated] {
                auto buf = updates_view_->get_buffer();
                if (sync_status == 0) {
                    buf->set_text(outdated.empty() ? "System is up to date." : outdated);
                    term_->log(outdated.empty() ? "\n=> System is up to date.\n" : "\n=> Updates found.\n");
                } else {
                    buf->set_text("Could not refresh package databases.");
                    term_->log("\n=> Failed to refresh package databases (exit code " +
                               std::to_string(sync_status) + ").\n");
                }
                bool any = sync_status == 0 && !sig::trim(outdated).empty();
                update_btn_->set_sensitive(any);
                busy_ = false;
                check_btn_->set_sensitive(true);
            });
        }).detach();
    }

    void on_update() {
        if (busy_) return;
        busy_ = true;
        update_btn_->set_sensitive(false);
        check_btn_->set_sensitive(false);
        status_label_->set_text("");
        std::thread([this] {
            if (db_locked()) {
                post([this] {
                    term_->log("\n(pacman database is locked - close other package managers, then retry)\n");
                    term_->log("\n==> Update aborted - database locked.\n");
                    busy_ = false;
                    check_btn_->set_sensitive(true);
                    update_btn_->set_sensitive(true);
                });
                return;
            }

            std::vector<std::string> args = pacman_cmd();
            args.insert(args.end(), {"-Syu", "--noconfirm", "--disable-download-timeout"});
            auto stream_out = [this](const char* line) {
                std::string s(line);
                post([this, s] { term_->log(s); });
            };
            term_->log("\n==> Running: pkexec pacman -Syu --noconfirm\n");
            int status = sig::run_stream(args, stream_out);
            if (status != 0) {
                term_->log("\n==> pacman reported a problem. Retrying once...\n");
                status = sig::run_stream(args, stream_out);
            }
            std::string remaining;
            if (status == 0) {
                remaining = available_updates();
            }
            bool reboot = needs_reboot();
            std::string msg;
            if (status == 0 && sig::trim(remaining).empty()) {
                msg = "System fully up to date.";
            } else if (status == 0) {
                msg = "Update finished, but some packages are still pending:\n" + remaining;
            } else {
                msg = "Update FAILED (exit code " + std::to_string(status) +
                      "). Nothing was silently accepted - re-run to try again.";
            }
            if (reboot) msg += "\n\nA new kernel is installed - please reboot to complete the update.";
            post([this, msg] {
                term_->log("\n==> " + msg + "\n");
                status_label_->set_text(msg);
                busy_ = false;
                check_btn_->set_sensitive(true);
                update_btn_->set_sensitive(true);
            });
        }).detach();
    }
};

int main(int argc, char** argv) {
    auto app = Gtk::Application::create("org.sigeonos.updater");
    app->make_window_and_run<UpdaterWindow>(argc, argv);
}