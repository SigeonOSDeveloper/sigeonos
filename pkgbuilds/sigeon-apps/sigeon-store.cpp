#include "common.hpp"
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <libxml/xmlreader.h>

namespace {

constexpr const char* CATALOG_URL =
    "https://dl.flathub.org/repo/appstream/x86_64/appstream.xml.gz";
constexpr const char* ICON_BASE =
    "https://dl.flathub.org/repo/appstream/x86_64/icons/128x128/";

const char* POPULAR_IDS[] = {
    "com.spotify.Client",
    "com.discordapp.Discord",
    "org.videolan.VLC",
    "org.mozilla.firefox",
    "org.chromium.Chromium",
    "com.google.Chrome",
    "com.visualstudio.code",
    "org.gnome.GIMP",
    "org.blender.Blender",
    "com.obsproject.Studio",
    "com.valvesoftware.Steam",
    "net.lutris.Lutris",
    "org.libreoffice.LibreOffice",
    "org.godotengine.Godot",
    "org.audacityteam.Audacity",
    "org.telegram.desktop",
    "org.signal.Signal",
    "us.zoom.Zoom",
    "com.heroicgameslauncher.hgl",
    "io.github.shiftey.Desktop",
    "org.geany.Geany",
    "org.inkscape.Inkscape",
    "com.jetbrains.IntelliJ-IDEA-Community",
    "com.jetbrains.PyCharm-Community",
    "io.mpv.Mpv",
    "org.qbittorrent.qBittorrent",
    "com.github.alainm23.planify",
    "org.nickvision.tubeconverter",
    "com.github.micahflee.torbrowser-launcher",
    "org.videolan.risky",
    "org.filezillaproject.Filezilla",
};

struct AppInfo {
    std::string appid;
    std::string name;
    std::string summary;
    std::string developer;
    std::string license;
    std::string icon;
    std::vector<std::string> categories;
    bool desktop = false;
    // localization fallbacks
    std::string en_name, en_summary, first_name, first_summary;
};

// --- libxml2 streaming parse of the gzipped appstream catalog ---
std::string node_text(xmlTextReaderPtr r) {
    xmlChar* s = xmlTextReaderReadString(r);
    std::string out = s ? (const char*)s : "";
    if (s) xmlFree(s);
    return sig::trim(out);
}

std::vector<AppInfo> parse_appstream(const std::string& path) {
    std::vector<AppInfo> apps;
    xmlTextReaderPtr r = xmlReaderForFile(path.c_str(), nullptr, 0);
    if (!r) return apps;

    int ret;
    AppInfo cur;
    bool in_component = false;
    while ((ret = xmlTextReaderRead(r)) == 1) {
        int t = xmlTextReaderNodeType(r);
        const xmlChar* ln = xmlTextReaderConstLocalName(r);
        std::string n = ln ? (const char*)ln : "";

        if (t == XML_READER_TYPE_ELEMENT) {
            if (n == "component") {
                in_component = true;
                cur = AppInfo();
                xmlChar* ctype = xmlTextReaderGetAttribute(r, (const xmlChar*)"type");
                cur.desktop = ctype && xmlStrEqual(ctype, (const xmlChar*)"desktop-application");
                if (ctype) xmlFree(ctype);
            } else if (in_component && n == "id") {
                if (cur.appid.empty()) cur.appid = node_text(r);
            } else if (in_component && n == "name") {
                xmlChar* lang = xmlTextReaderGetAttribute(r, (const xmlChar*)"xml:lang");
                std::string v = node_text(r);
                if (!lang && cur.name.empty()) cur.name = v;
                else if (cur.en_name.empty() && lang &&
                         (xmlStrncmp(lang, (const xmlChar*)"en", 2) == 0))
                    cur.en_name = v;
                if (cur.first_name.empty()) cur.first_name = v;
                if (lang) xmlFree(lang);
            } else if (in_component && n == "summary") {
                xmlChar* lang = xmlTextReaderGetAttribute(r, (const xmlChar*)"xml:lang");
                std::string v = node_text(r);
                if (!lang && cur.summary.empty()) cur.summary = v;
                else if (cur.en_summary.empty() && lang &&
                         (xmlStrncmp(lang, (const xmlChar*)"en", 2) == 0))
                    cur.en_summary = v;
                if (cur.first_summary.empty()) cur.first_summary = v;
                if (lang) xmlFree(lang);
            } else if (in_component && n == "developer_name") {
                if (cur.developer.empty()) cur.developer = node_text(r);
            } else if (in_component && n == "project_license") {
                if (cur.license.empty()) cur.license = node_text(r);
            } else if (in_component && n == "category") {
                std::string c = node_text(r);
                if (!c.empty() && std::find(cur.categories.begin(), cur.categories.end(), c)
                        == cur.categories.end()) {
                    cur.categories.push_back(c);
                }
            } else if (in_component && n == "icon") {
                xmlChar* type = xmlTextReaderGetAttribute(r, (const xmlChar*)"type");
                if (type && xmlStrEqual(type, (const xmlChar*)"cached")) {
                    std::string ic = node_text(r);
                    if (!ic.empty() && ic.size() > 4 &&
                        ic.substr(ic.size() - 4) == ".png") {
                        cur.icon = ic;
                    }
                }
                if (type) xmlFree(type);
            }
        } else if (t == XML_READER_TYPE_END_ELEMENT) {
            if (in_component && n == "component") {
                in_component = false;
                if (cur.name.empty()) cur.name = cur.en_name.empty() ? cur.first_name : cur.en_name;
                if (cur.summary.empty()) cur.summary = cur.en_summary.empty() ? cur.first_summary : cur.en_summary;
                if (cur.desktop && !cur.appid.empty() && !cur.name.empty()) {
                    apps.push_back(std::move(cur));
                }
                cur = AppInfo();
            }
        }
    }
    xmlFreeTextReader(r);
    return apps;
}

std::string to_lower(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)tolower((unsigned char)c);
    return o;
}

} // namespace

class StoreWindow : public Gtk::Window {
public:
    StoreWindow() {
        set_title("Sigeon Store");
        set_default_size(980, 720);

        cache_dir_ = std::string(getenv("HOME")) + "/.cache/sigeon-store";
        std::filesystem::create_directories(cache_dir_ + "/icons");
        catalog_path_ = cache_dir_ + "/appstream.xml.gz";

        auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        root->set_margin(12);

        // Header
        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto* title = Gtk::make_managed<Gtk::Label>();
        title->set_markup("<span size='x-large' weight='bold'>Sigeon Store</span>");
        title->set_xalign(0);
        title->set_hexpand(true);
        refresh_btn_ = Gtk::make_managed<Gtk::Button>("Refresh catalog");
        refresh_btn_->signal_clicked().connect(sigc::mem_fun(*this, &StoreWindow::on_refresh));
        header->append(*title);
        header->append(*refresh_btn_);
        root->append(*header);

        // Search + category
        auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        search_entry_ = Gtk::make_managed<Gtk::Entry>();
        search_entry_->set_placeholder_text("Search Flathub (Spotify, Discord, ...)");
        search_entry_->set_hexpand(true);
        search_entry_->signal_changed().connect(sigc::mem_fun(*this, &StoreWindow::on_search_changed));
        search_entry_->signal_activate().connect(sigc::mem_fun(*this, &StoreWindow::on_search_activate));
        cat_combo_ = Gtk::make_managed<Gtk::DropDown>();
        cat_model_ = Gtk::StringList::create({std::string("All apps")});
        cat_combo_->set_model(cat_model_);
        cat_combo_->signal_activate().connect(
            sigc::mem_fun(*this, &StoreWindow::on_category_changed));
        cat_combo_->property_selected().signal_changed().connect(
            sigc::mem_fun(*this, &StoreWindow::on_category_changed));
        bar->append(*search_entry_);
        bar->append(*cat_combo_);
        root->append(*bar);

        // Grid of app tiles
        scroll_ = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        scroll_->set_vexpand(true);
        flow_ = Gtk::make_managed<Gtk::FlowBox>();
        flow_->set_max_children_per_line(6);
        flow_->set_min_children_per_line(2);
        flow_->set_selection_mode(Gtk::SelectionMode::NONE);
        flow_->set_homogeneous(true);
        flow_->set_column_spacing(8);
        flow_->set_row_spacing(8);
        scroll_->set_child(*flow_);
        root->append(*scroll_);

        // Status + log
        status_label_ = Gtk::make_managed<Gtk::Label>();
        status_label_->set_xalign(0);
        status_label_->get_style_context()->add_class("dim-label");
        root->append(*status_label_);

        output_view_ = Gtk::make_managed<Gtk::TextView>();
        output_view_->set_editable(false);
        output_view_->set_monospace(true);
        output_view_->set_wrap_mode(Gtk::WrapMode::CHAR);
        output_view_->set_size_request(-1, 120);
        auto* oscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        oscroll->set_child(*output_view_);
        auto* oframe = Gtk::make_managed<Gtk::Frame>("Activity log");
        oframe->set_child(*oscroll);
        term_ = std::make_shared<Term>(*output_view_);

        root->append(*oframe);
        set_child(*root);

        disp_.connect([this] { drain(); });
        status_label_->set_text("Loading Flathub catalog...");
        refresh_btn_->set_sensitive(false);

        // Background: fetch catalog + installed list
        std::thread([this] {
            std::string dlout = ensure_catalog();
            std::string inst = installed_list();
            auto apps = parse_appstream(catalog_path_);
            post([this, apps, inst, dlout] {
                catalog_ = std::move(apps);
                std::istringstream ss(inst);
                std::string line;
                while (std::getline(ss, line)) {
                    std::string id = sig::trim(line);
                    if (!id.empty() && id != "Application") installed_.insert(id);
                }
                if (!dlout.empty()) term_->log("Catalog: " + dlout + "\n");
                rebuild_categories();
                refresh_btn_->set_sensitive(true);
                rebuild_tiles();
                status_label_->set_text(std::to_string(catalog_.size()) +
                    " apps available, " + std::to_string(installed_.size()) + " installed.");
            });
        }).detach();
    }

private:
    std::string cache_dir_;
    std::string catalog_path_;

    Gtk::Entry* search_entry_ = nullptr;
    Gtk::Button* refresh_btn_ = nullptr;
    Gtk::DropDown* cat_combo_ = nullptr;
    Glib::RefPtr<Gtk::StringList> cat_model_;
    Gtk::ScrolledWindow* scroll_ = nullptr;
    Gtk::FlowBox* flow_ = nullptr;
    Gtk::Label* status_label_ = nullptr;
    Gtk::TextView* output_view_ = nullptr;
    std::shared_ptr<Term> term_;
    bool busy_ = false;

    std::vector<AppInfo> catalog_;
    std::set<std::string> installed_;
    std::map<std::string, Glib::RefPtr<Gdk::Texture>> tex_cache_;
    std::map<std::string, Gtk::Image*> pic_map_; // current tiles appid->image
    std::set<std::string> icon_inflight_;

    Glib::Dispatcher disp_;
    std::mutex mtx_;
    std::vector<std::function<void()>> queue_;
    bool closing_ = false;

    // icon downloader pool
    std::deque<std::string> icon_queue_;
    std::mutex icon_mtx_;
    std::condition_variable icon_cv_;
    bool icon_pool_done_ = false;
    unsigned icon_workers_ = 0;
    std::vector<std::thread> icon_worker_threads_;

    ~StoreWindow() override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            closing_ = true;
        }
        {
            std::lock_guard<std::mutex> lk(icon_mtx_);
            icon_pool_done_ = true;
        }
        icon_cv_.notify_all();
        for (auto& t : icon_worker_threads_) if (t.joinable()) t.join();
    }

    void post(const std::function<void()>& fn) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (closing_) return;
            queue_.push_back(fn);
        }
        disp_.emit();
    }

    void drain() {
        std::vector<std::function<void()>> tmp;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (closing_) return;
            tmp.swap(queue_);
        }
        for (auto& fn : tmp) fn();
    }

    std::string ensure_catalog() {
        struct stat st{};
        bool ok = ::stat(catalog_path_.c_str(), &st) == 0;
        if (ok && time(nullptr) - st.st_mtime < 6 * 3600)
            return "using cached catalog";
        std::string out = sig::run_capture(
            {"curl", "-sS", "-f", "-L", "--max-time", "120", "-o", catalog_path_, CATALOG_URL});
        if (out.empty()) return "catalog up to date";
        return "updated (" + sig::trim(out) + ")";
    }

    std::string installed_list() {
        return sig::run_capture({"flatpak", "list", "--app", "--columns=application"});
    }

    void rebuild_categories() {
        std::set<std::string> cats;
        for (const auto& a : catalog_)
            for (const auto& c : a.categories) cats.insert(c);
        cat_model_ = Gtk::StringList::create({});
        cat_model_->append("All apps");
        for (const auto& c : cats) cat_model_->append(c);
        cat_combo_->set_model(cat_model_);
        cat_combo_->set_selected(0);
    }

    std::vector<const AppInfo*> filtered() {
        std::string q = to_lower(search_entry_->get_text().raw());
        std::string cat = cat_model_->get_string(cat_combo_->get_selected()).raw();
        bool popular = q.empty() && (cat.empty() || cat == "All apps");

        std::vector<const AppInfo*> out;
        size_t limit = popular ? POPULAR_COUNT() : 400;
        if (popular) {
            for (const char* id : POPULAR_IDS) {
                for (const auto& a : catalog_) {
                    if (a.appid == id) { out.push_back(&a); break; }
                }
            }
            return out;
        }
        for (const auto& a : catalog_) {
            if (limit == 0) break;
            if (!cat.empty() && cat != "All apps") {
                auto it = std::find(a.categories.begin(), a.categories.end(), cat);
                if (it == a.categories.end()) continue;
            }
            if (!q.empty()) {
                std::string hay = to_lower(a.name + " " + a.summary + " " + a.appid + " " + a.developer);
                if (hay.find(q) == std::string::npos) continue;
            }
            out.push_back(&a);
            if (--limit == 0) break;
        }
        return out;
    }

    static size_t POPULAR_COUNT() { return sizeof(POPULAR_IDS) / sizeof(POPULAR_IDS[0]); }

    void on_search_changed() { rebuild_tiles(); }

    void on_search_activate() {
        std::string q = sig::trim(search_entry_->get_text().raw());
        if (q.empty()) return;
        std::string ql = to_lower(q);
        const AppInfo* best = nullptr;
        int best_score = -1;
        for (const auto& a : catalog_) {
            std::string name = to_lower(a.name);
            std::string id = to_lower(a.appid);
            std::string sum = to_lower(a.summary);
            int score = 0;
            if (id == ql) score = 10000;
            else if (name == ql) score = 8000;
            else if (id.find(ql) == 0) score = 5000;
            else if (name.find(ql) == 0) score = 4000;
            else if (id.find(ql) != std::string::npos) score = 3000;
            else if (name.find(ql) != std::string::npos) score = 2000;
            else if (sum.find(ql) != std::string::npos) score = 1000;
            if (score > best_score) { best_score = score; best = &a; }
        }
        if (!best) {
            status_label_->set_text("No application matches \"" + q + ".\"");
            return;
        }
        show_app_dialog(*best);
    }

    void show_app_dialog(const AppInfo& a) {
        auto* dlg = Gtk::make_managed<Gtk::Dialog>();
        dlg->set_transient_for(*this);
        dlg->set_modal(true);
        dlg->set_title(a.name);
        dlg->set_default_size(440, -1);

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
        box->set_margin(16);

        auto* img = Gtk::make_managed<Gtk::Image>();
        img->set_size_request(96, 96);
        auto tex = tex_cache_.find(a.appid);
        if (tex != tex_cache_.end()) img->set(tex->second);
        else img->set_from_icon_name("applications-other");
        box->append(*img);

        auto* v = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        auto* name = Gtk::make_managed<Gtk::Label>();
        name->set_markup(Glib::ustring::compose("<span size='x-large' weight='bold'>%1</span>",
            Glib::Markup::escape_text(a.name)));
        name->set_xalign(0);
        name->set_wrap(true);
        v->append(*name);

        auto* idl = Gtk::make_managed<Gtk::Label>("<span size='small'>" + Glib::ustring(a.appid) + "</span>");
        idl->set_markup(Glib::ustring::compose("<span size='small' weight='bold'>%1</span>", a.appid));
        idl->set_xalign(0);
        idl->set_selectable(true);
        v->append(*idl);

        auto* sum = Gtk::make_managed<Gtk::Label>(a.summary);
        sum->set_xalign(0);
        sum->set_wrap(true);
        sum->set_selectable(true);
        v->append(*sum);

        Glib::ustring meta = (a.developer.empty() ? "Unknown developer" : a.developer);
        if (!a.license.empty()) meta += "  |  License: " + a.license;
        auto* m = Gtk::make_managed<Gtk::Label>();
        m->set_markup("<span size='small'>" + Glib::Markup::escape_text(meta) + "</span>");
        m->set_xalign(0);
        m->get_style_context()->add_class("dim-label");
        v->append(*m);

        bool inst = installed_.count(a.appid) > 0;
        auto* btns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btns->set_halign(Gtk::Align::START);
        auto* act = Gtk::make_managed<Gtk::Button>(inst ? "Remove" : "Install");
        if (inst) act->get_style_context()->add_class("destructive-action");
        else act->get_style_context()->add_class("suggested-action");
        act->signal_clicked().connect(sigc::bind(
            sigc::mem_fun(*this, &StoreWindow::on_toggle), a.appid));
        auto* close = Gtk::make_managed<Gtk::Button>("Close");
        close->signal_clicked().connect([dlg] { dlg->close(); });
        btns->append(*act);
        btns->append(*close);
        v->append(*btns);

        box->append(*v);
        dlg->get_content_area()->append(*box);

        // queue icon download so the dialog icon arrives live
        if (tex == tex_cache_.end()) queue_icon(a.appid, a.icon);

        dlg->show();
    }

    void on_category_changed() { rebuild_tiles(); }

    void rebuild_tiles() {
        for (auto* ch : flow_->get_children()) flow_->remove(*ch);
        pic_map_.clear();

        auto apps = filtered();
        if (apps.empty()) {
            status_label_->set_text("No applications match your search.");
            return;
        }
        int shown = 0;
        for (const auto* a : apps) {
            shown++;
            auto* tile = make_tile(*a);
            flow_->append(*tile);
            queue_icon(a->appid, a->icon);
        }
        status_label_->set_text(std::to_string(shown) + " application" +
            (shown == 1 ? "" : "s") + " shown.");
    }

    Gtk::Box* make_tile(const AppInfo& a) {
        auto* tile = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        tile->set_size_request(150, -1);
        tile->set_margin(4);

        auto* pic = Gtk::make_managed<Gtk::Image>();
        pic->set_size_request(64, 64);
        pic->set_margin_top(4);
        auto tex = tex_cache_.find(a.appid);
        if (tex != tex_cache_.end()) pic->set(tex->second);
        else pic->set_from_icon_name("applications-other");
        pic_map_[a.appid] = pic;
        tile->append(*pic);

        auto* name = Gtk::make_managed<Gtk::Label>();
        name->set_markup(Glib::ustring::compose("<b>%1</b>",
            Glib::Markup::escape_text(a.name)));
        name->set_xalign(0.5f);
        name->set_wrap(true);
        name->set_lines(2);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        tile->append(*name);

        auto* sum = Gtk::make_managed<Gtk::Label>(a.summary);
        sum->set_xalign(0.5f);
        sum->set_wrap(true);
        sum->set_lines(2);
        sum->set_ellipsize(Pango::EllipsizeMode::END);
        sum->get_style_context()->add_class("dim-label");
        tile->append(*sum);

        bool inst = installed_.count(a.appid) > 0;
        auto* btn = Gtk::make_managed<Gtk::Button>(inst ? "Remove" : "Install");
        if (inst) btn->get_style_context()->add_class("destructive-action");
        else btn->get_style_context()->add_class("suggested-action");
        btn->signal_clicked().connect(sigc::bind(
            sigc::mem_fun(*this, &StoreWindow::on_toggle), a.appid));
        auto* btnbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        btnbox->set_halign(Gtk::Align::CENTER);
        btnbox->set_margin_bottom(6);
        btnbox->append(*btn);
        tile->append(*btnbox);

        tile->set_tooltip_text(a.appid + (a.developer.empty() ? "" : "\nby " + a.developer));
        return tile;
    }

    void on_toggle(const std::string& appid) {
        if (busy_) return;
        bool inst = installed_.count(appid) > 0;
        busy_ = true;
        term_->log((inst ? "Removing " : "Installing ") + appid + "...\n");
        std::thread([this, appid, inst] {
            std::string cmd_str;
            std::vector<std::string> args;
            if (inst) {
                args = {"pkexec", "flatpak", "uninstall", "-y", "--system", "--noninteractive", appid};
            } else {
                std::string install_cmd =
                    "flatpak remote-add --if-not-exists --system flathub "
                    "https://flathub.org/repo/flathub.flatpkgrepo && "
                    "flatpak install -y --system --noninteractive --or-update flathub " + appid;
                args = {"pkexec", "sh", "-c", "'" + install_cmd + "'"};
            }
            for (const auto& a : args) cmd_str += a + " ";
            term_->log("$ " + cmd_str + "\n");
            int status = sig::run_stream(args, [this](const char* line) {
                std::string s(line);
                this->post([this, s] { this->term_->log(s); });
            });
            post([this, appid, inst, status] {
                busy_ = false;
                if (status == 0) {
                    term_->log("\nOperation completed successfully.\n");
                    if (inst) installed_.erase(appid);
                    else installed_.insert(appid);
                    status_label_->set_text(inst ? "Application removed." : "Application installed.");
                } else {
                    term_->log("\nOperation failed with exit code " + std::to_string(status) + ".\n");
                    status_label_->set_text("Operation failed.");
                }
                rebuild_tiles();
            });
        }).detach();
    }

    void on_refresh() {
        if (busy_) return;
        busy_ = true;
        refresh_btn_->set_sensitive(false);
        status_label_->set_text("Refreshing catalog...");
        term_->log("Refreshing Flathub catalog...\n");
        std::thread([this] {
            std::string dlout = sig::run_capture({"curl", "-sS", "-f", "-L", "--max-time", "120", "-o", catalog_path_, CATALOG_URL});
            auto apps = parse_appstream(catalog_path_);
            post([this, apps, dlout] {
                busy_ = false;
                catalog_ = std::move(apps);
                if (!dlout.empty()) term_->log("Catalog: " + dlout + "\n");
                rebuild_categories();
                rebuild_tiles();
                refresh_btn_->set_sensitive(true);
                status_label_->set_text("Catalog refreshed: " + std::to_string(catalog_.size()) + " apps.");
            });
        }).detach();
    }

    // --- icon downloader pool ---
    void queue_icon(const std::string& appid, const std::string& icontxt) {
        if (icontxt.empty()) return;
        {
            std::lock_guard<std::mutex> lk(icon_mtx_);
            if (tex_cache_.count(appid) || icon_inflight_.count(appid)) return;
            icon_inflight_.insert(appid);
            icon_queue_.push_back(appid + "|" + icontxt);
            if (icon_workers_ < 6) {
                icon_workers_++;
                icon_worker_threads_.emplace_back([this] { icon_worker(); });
            }
        }
        icon_cv_.notify_one();
    }

    void icon_worker() {
        for (;;) {
            std::string item;
            {
                std::unique_lock<std::mutex> lk(icon_mtx_);
                icon_cv_.wait(lk, [this] {
                    return icon_pool_done_ || !icon_queue_.empty();
                });
                if (icon_pool_done_) return;
                item = icon_queue_.front();
                icon_queue_.pop_front();
            }
            size_t pipe = item.find('|');
            if (pipe == std::string::npos) continue;
            std::string appid = item.substr(0, pipe);
            std::string ic = item.substr(pipe + 1);
            std::string path = cache_dir_ + "/icons/" + ic;
            if (!std::filesystem::exists(path)) {
                sig::run_capture({"curl", "-sS", "-f", "-L", "--connect-timeout",
                    "10", "--max-time", "60", "-o", path, ICON_BASE + ic});
            }
            post([this, appid, path] {
                icon_inflight_.erase(appid);
                if (!std::filesystem::exists(path)) return;
                try {
                    auto tex = Gdk::Texture::create_from_filename(path);
                    tex_cache_[appid] = tex;
                    auto it = pic_map_.find(appid);
                    if (it != pic_map_.end()) it->second->set(tex);
                } catch (const Glib::Error&) {}
            });
        }
    }
};

int main(int argc, char** argv) {
    auto app = Gtk::Application::create("org.sigeonos.store");
    app->make_window_and_run<StoreWindow>(argc, argv);
    return 0;
}