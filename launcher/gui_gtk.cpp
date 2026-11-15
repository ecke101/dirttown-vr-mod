#include "gui.hpp"
#include <gtk/gtk.h>
#include <functional>

namespace launcher {
class GtkApp {
    GuiModel model;
    GtkWidget *window = nullptr, *game = nullptr, *proton = nullptr, *runtime = nullptr,
              *scale = nullptr, *graphics = nullptr;
    GtkWidget *controllers = nullptr, *taa = nullptr, *status = nullptr, *resolution = nullptr;
    GtkWidget *statusBox = nullptr, *errorLogs = nullptr, *launch = nullptr;
    GtkWidget *advanced = nullptr, *diagnostics = nullptr;
    bool failed = false;
    std::vector<GtkWidget*> actions;
    std::vector<double> scales{.25, .5, .625, .75, 1, 1.25, 1.5};
    static void style(GtkWidget* widget, const char* name) {
        gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
    }
    void error(const std::string& message) {
        failed = true;
        model.status = message;
        refresh();
    }
    void attempt(const std::function<void()>& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            error(e.what());
        }
    }
    Config preferences() {
        Config c = model.config;
        c.game = gtk_entry_get_text(GTK_ENTRY(game));
        c.proton = gtk_entry_get_text(GTK_ENTRY(proton));
        c.runtime = gtk_entry_get_text(GTK_ENTRY(runtime));
        c.scale = scales.at((size_t)gtk_combo_box_get_active(GTK_COMBO_BOX(scale)));
        const char* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(graphics));
        c.graphics = id ? id : "auto";
        c.controllers = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controllers));
        c.antialiasing = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(taa));
        return c;
    }
    void refresh() {
        const bool available = model.available();
        const auto message = !available && !model.pending && !failed
                                 ? "Dirt Town is already open. Close it, then launch VR from here."
                                 : model.status;
        if (message != gtk_label_get_text(GTK_LABEL(status)))
            gtk_label_set_text(GTK_LABEL(status), message.c_str());
        auto context = gtk_widget_get_style_context(statusBox);
        if (failed)
            gtk_style_context_add_class(context, "error");
        else
            gtk_style_context_remove_class(context, "error");
        gtk_widget_set_visible(errorLogs, failed);
        const auto label =
            model.resolution(scales.at((size_t)gtk_combo_box_get_active(GTK_COMBO_BOX(scale))));
        if (label != gtk_label_get_text(GTK_LABEL(resolution)))
            gtk_label_set_text(GTK_LABEL(resolution), label.c_str());
        for (auto button : actions)
            gtk_widget_set_sensitive(button, available);
    }
    static GtkWidget* label(const char* text) {
        auto w = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(w), 0);
        gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
        return w;
    }
    GtkWidget* field(GtkWidget* box, const char* title, const std::string& value, bool directory) {
        gtk_box_pack_start(GTK_BOX(box), label(title), FALSE, FALSE, 0);
        auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        auto entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
        gtk_widget_set_hexpand(entry, TRUE);
        gtk_box_pack_start(GTK_BOX(row), entry, TRUE, TRUE, 0);
        auto browse = gtk_button_new_with_label("Browse...");
        gtk_box_pack_start(GTK_BOX(row), browse, FALSE, FALSE, 0);
        struct Browse {
            GtkApp* app;
            GtkWidget* entry;
            bool directory;
        };
        auto data = new Browse{this, entry, directory};
        g_signal_connect_data(
            browse, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto& d = *static_cast<Browse*>(p);
                auto chooser =
                    gtk_file_chooser_native_new("Select a path", GTK_WINDOW(d.app->window),
                                                d.directory ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                                                            : GTK_FILE_CHOOSER_ACTION_OPEN,
                                                "Select", "Cancel");
                const char* current = gtk_entry_get_text(GTK_ENTRY(d.entry));
                if (*current)
                    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), current);
                if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
                    char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
                    if (file)
                        gtk_entry_set_text(GTK_ENTRY(d.entry), file);
                    g_free(file);
                }
                g_object_unref(chooser);
            }),
            data,
            +[](gpointer p, GClosure*) {
                delete static_cast<Browse*>(p);
            },
            G_CONNECT_DEFAULT);
        return entry;
    }
    GtkWidget* button(GtkWidget* box, const char* title, const char* command) {
        auto w = gtk_button_new_with_label(title);
        gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
        actions.push_back(w);
        g_object_set_data(G_OBJECT(w), "command", (gpointer)command);
        g_signal_connect(w, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer data) {
                             auto self = static_cast<GtkApp*>(data);
                             self->attempt([&] {
                                 self->model.start(
                                     (const char*)g_object_get_data(G_OBJECT(b), "command"),
                                     self->preferences());
                                 self->failed = false;
                                 self->refresh();
                             });
                         }),
                         this);
        return w;
    }
    GtkWidget* openButton(GtkWidget* box, const char* title, const fs::path& target) {
        auto w = gtk_button_new_with_label(title);
        gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
        struct Target {
            GtkApp* app;
            fs::path path;
        };
        auto data = new Target{this, target};
        g_signal_connect_data(
            w, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto& d = *static_cast<Target*>(p);
                d.app->attempt([&] {
                    openPath(d.path);
                });
            }),
            data,
            +[](gpointer p, GClosure*) {
                delete static_cast<Target*>(p);
            },
            G_CONNECT_DEFAULT);
        return w;
    }
    void capture(const char* name) {
        auto surface =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gtk_widget_get_allocated_width(window),
                                       gtk_widget_get_allocated_height(window));
        auto cr = cairo_create(surface);
        gtk_widget_draw(window, cr);
        cairo_destroy(cr);
        cairo_surface_write_to_png(surface, text(model.dirs.logs / name).c_str());
        cairo_surface_destroy(surface);
    }

  public:
    GtkApp(const Paths& dirs, Config config) : model(dirs, config) {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), "Dirt Town VR Mod");
        gtk_window_set_default_size(GTK_WINDOW(window), 740, 680);
        gtk_widget_set_size_request(window, 620, 480);
        g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
                             gtk_main_quit();
                         }),
                         nullptr);
        gtk_widget_set_name(window, "dirttown-launcher");
        auto provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, R"CSS(
#dirttown-launcher { background: #171c1e; color: #f3f1e9; font-family: sans-serif; font-size: 14px; }
#dirttown-launcher label { color: #f3f1e9; }
#dirttown-launcher .muted { color: #b6c0c2; }
#dirttown-launcher .title { font-size: 29px; font-weight: 700; }
#dirttown-launcher .eyebrow { color: #efb354; font-size: 11px; font-weight: 700; }
#dirttown-launcher .section-title { font-weight: 600; }
#dirttown-launcher .card { background: #22292c; border: 1px solid #364044; border-radius: 12px; padding: 16px; }
#dirttown-launcher entry, #dirttown-launcher combobox button {
    background: #202729; color: #f3f1e9; border: 1px solid #4b585d; border-radius: 7px;
    box-shadow: none; min-height: 24px; padding: 7px 10px;
}
#dirttown-launcher entry:focus, #dirttown-launcher button:focus {
    border-color: #efb354; outline-color: #efb354; outline-width: 2px; outline-offset: 2px;
}
#dirttown-launcher button {
    background: #2c3539; background-image: none; color: #f3f1e9; border: 1px solid #4b585d;
    border-radius: 8px; padding: 9px 14px; min-height: 22px; box-shadow: none; text-shadow: none;
}
#dirttown-launcher button:hover { background: #3b474c; border-color: #7d8b90; }
#dirttown-launcher button:active { background: #202729; }
#dirttown-launcher button:disabled { background: #252d30; color: #849196; border-color: #364044; }
#dirttown-launcher button.primary {
    background: #efb354; color: #201c15; border-color: #efb354; min-height: 34px;
    font-size: 18px; font-weight: 700; padding: 10px 24px;
}
#dirttown-launcher button.primary label { color: inherit; }
#dirttown-launcher button.primary:hover { background: #ffc571; border-color: #ffc571; }
#dirttown-launcher button.primary:active { background: #dda044; }
#dirttown-launcher button.primary:disabled { background: #394346; color: #a5b0b4; border-color: #394346; }
#dirttown-launcher button.quiet { background: transparent; border-color: transparent; padding: 5px 8px; color: #c5ced0; }
#dirttown-launcher button.quiet:hover { background: #343e42; color: #ffffff; }
#dirttown-launcher .status { background: #222b2d; border-radius: 8px; padding: 12px; }
#dirttown-launcher .status.error { background: #432b28; }
#dirttown-launcher .status.error label { color: #ffd7c9; }
#dirttown-launcher checkbutton { padding: 4px 0; }
#dirttown-launcher check { border-radius: 4px; border-color: #7d8b90; }
#dirttown-launcher check:checked { background: #efb354; color: #201c15; border-color: #efb354; }
#dirttown-launcher expander > title { padding: 9px 0; }
#dirttown-launcher expander > title > label { font-weight: 600; }
#dirttown-launcher .footnote { font-size: 12px; color: #b6c0c2; }
)CSS",
                                        -1, nullptr);
        gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(window),
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                       GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(window), scroll);
        auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(box), 24);
        gtk_container_add(GTK_CONTAINER(scroll), box);
        auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        auto heading = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_box_pack_start(GTK_BOX(row), heading, TRUE, TRUE, 0);
        auto title = label("Dirt Town VR");
        style(title, "title");
        gtk_box_pack_start(GTK_BOX(heading), title, FALSE, FALSE, 0);
        auto subtitle = label("Your next shift, from the driver's seat.");
        style(subtitle, "muted");
        gtk_box_pack_start(GTK_BOX(heading), subtitle, FALSE, FALSE, 0);
        auto badge = label("OPENXR");
        style(badge, "eyebrow");
        gtk_widget_set_valign(badge, GTK_ALIGN_START);
        gtk_widget_set_margin_top(badge, 10);
        gtk_box_pack_end(GTK_BOX(row), badge, FALSE, FALSE, 0);
        auto gameBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_box_pack_start(GTK_BOX(box), gameBox, FALSE, FALSE, 4);
        game = field(gameBox, "Game installation", config.game, true);
        gtk_widget_set_tooltip_text(game, "Select the folder containing Dirt Town Demo.");
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        launch = button(row, "Launch VR", "launch");
        gtk_box_set_child_packing(GTK_BOX(row), launch, TRUE, TRUE, 0, GTK_PACK_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(launch), "suggested-action");
        style(launch, "primary");
        gtk_widget_set_can_default(launch, TRUE);
        gtk_widget_grab_default(launch);
        button(row, "Detect headset", "runtime");
        statusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        style(statusBox, "status");
        gtk_box_pack_start(GTK_BOX(box), statusBox, FALSE, FALSE, 0);
        status = label(model.status.c_str());
        gtk_label_set_max_width_chars(GTK_LABEL(status), 64);
        gtk_label_set_selectable(GTK_LABEL(status), TRUE);
        gtk_box_pack_start(GTK_BOX(statusBox), status, TRUE, TRUE, 0);
        errorLogs = openButton(statusBox, "Open logs", model.dirs.logs);
        style(errorLogs, "quiet");
        gtk_widget_set_no_show_all(errorLogs, TRUE);
        auto options = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        style(options, "card");
        gtk_box_pack_start(GTK_BOX(box), options, FALSE, FALSE, 0);
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(options), row, FALSE, FALSE, 0);
        auto section = label("Display & controls");
        style(section, "section-title");
        gtk_box_pack_start(GTK_BOX(row), section, TRUE, TRUE, 0);
        style(openButton(row, "Controller settings...", model.dirs.config / "controls.ini"),
              "quiet");
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(options), row, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), label("Render resolution"), FALSE, FALSE, 0);
        scale = gtk_combo_box_text_new();
        if (std::find(scales.begin(), scales.end(), config.scale) == scales.end())
            scales.push_back(config.scale);
        int selected = 0;
        for (size_t i = 0; i < scales.size(); ++i) {
            auto s = Json(scales[i] * 100).dump();
            s.erase(s.find_last_not_of('0') + 1);
            if (s.back() == '.')
                s.pop_back();
            s += '%';
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scale), s.c_str());
            if (scales[i] == config.scale)
                selected = (int)i;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(scale), selected);
        gtk_widget_set_tooltip_text(
            scale, "Lower the resolution if VR performance is poor. Start at 50–75%.");
        gtk_box_pack_start(GTK_BOX(row), scale, FALSE, FALSE, 0);
        resolution = label("");
        style(resolution, "muted");
        gtk_box_pack_start(GTK_BOX(row), resolution, TRUE, TRUE, 0);
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
        gtk_box_pack_start(GTK_BOX(options), row, FALSE, FALSE, 0);
        controllers = gtk_check_button_new_with_label("Use VR controllers");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controllers), config.controllers);
        gtk_box_pack_start(GTK_BOX(row), controllers, FALSE, FALSE, 0);
        taa = gtk_check_button_new_with_label("Antialiasing");
        gtk_widget_set_tooltip_text(
            taa,
            "Optional temporal antialiasing smooths edges. Off by default; F11 toggles it in game.");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(taa), config.antialiasing);
        gtk_box_pack_start(GTK_BOX(row), taa, FALSE, FALSE, 0);
        advanced = gtk_expander_new("Advanced setup");
        gtk_box_pack_start(GTK_BOX(box), advanced, FALSE, FALSE, 0);
        auto advancedBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_top(advancedBox, 8);
        gtk_widget_set_margin_bottom(advancedBox, 8);
        gtk_container_add(GTK_CONTAINER(advanced), advancedBox);
        runtime = field(advancedBox, "OpenXR runtime manifest", config.runtime, false);
        gtk_entry_set_placeholder_text(GTK_ENTRY(runtime), "Use the active OpenXR runtime");
        proton = field(advancedBox, "Proton executable", config.proton, false);
        gtk_entry_set_placeholder_text(GTK_ENTRY(proton), "Detect automatically from Steam");
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(advancedBox), row, FALSE, FALSE, 4);
        gtk_box_pack_start(GTK_BOX(row), label("Graphics backend"), FALSE, FALSE, 0);
        graphics = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(graphics), "auto", "Automatic");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(graphics), "d3d12", "D3D12");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(graphics), "d3d11", "D3D11");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(graphics), config.graphics.c_str());
        gtk_box_pack_start(GTK_BOX(row), graphics, FALSE, FALSE, 0);
        diagnostics = gtk_expander_new("Troubleshooting & controls");
        gtk_box_pack_start(GTK_BOX(box), diagnostics, FALSE, FALSE, 0);
        auto diagnosticsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(diagnosticsBox, 8);
        gtk_widget_set_margin_bottom(diagnosticsBox, 8);
        gtk_container_add(GTK_CONTAINER(diagnostics), diagnosticsBox);
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(diagnosticsBox), row, FALSE, FALSE, 0);
        button(row, "Test headset", "probe");
        button(row, "Test graphics", "gpu-test");
        openButton(row, "Open logs", model.dirs.logs);
        auto help = label(
            "After launching, load a site on your monitor. Menus and HUD stay on the monitor.\nRight stick: look while walking; control the implement in a dozer or loader.");
        style(help, "muted");
        gtk_box_pack_start(GTK_BOX(diagnosticsBox), help, FALSE, FALSE, 0);
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        auto shortcuts = label("");
        gtk_label_set_markup(GTK_LABEL(shortcuts),
                             "<b>F9</b> Recenter   <b>F10</b> VR on/off   <b>F11</b> Antialiasing");
        style(shortcuts, "footnote");
        gtk_box_pack_start(GTK_BOX(row), shortcuts, TRUE, TRUE, 0);
        auto version = label("v" DTVR_VERSION);
        style(version, "footnote");
        gtk_box_pack_start(GTK_BOX(row), version, FALSE, FALSE, 0);
        style(openButton(row, "Licences", model.dirs.cache / "licenses"), "quiet");
        g_signal_connect(scale, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer p) {
                             auto self = static_cast<GtkApp*>(p);
                             self->attempt([&] {
                                 self->refresh();
                             });
                         }),
                         this);
        refresh();
        gtk_widget_show_all(window);
        gtk_widget_grab_focus(launch);
    }
    void run(bool smoke) {
        auto timer = g_timeout_add(
            500,
            +[](gpointer p) -> gboolean {
                auto self = static_cast<GtkApp*>(p);
                self->attempt([&] {
                    if (self->model.poll())
                        self->failed = false;
                    self->refresh();
                });
                return G_SOURCE_CONTINUE;
            },
            this);
        if (smoke)
            g_timeout_add(
                1000,
                +[](gpointer p) -> gboolean {
                    auto self = static_cast<GtkApp*>(p);
                    self->capture("launcher-preview.png");
                    gtk_expander_set_expanded(GTK_EXPANDER(self->advanced), TRUE);
                    gtk_expander_set_expanded(GTK_EXPANDER(self->diagnostics), TRUE);
                    gtk_window_resize(GTK_WINDOW(self->window), 740, 1040);
                    g_timeout_add(
                        500,
                        +[](gpointer data) -> gboolean {
                            auto app = static_cast<GtkApp*>(data);
                            app->capture("launcher-advanced-preview.png");
                            gtk_widget_destroy(app->window);
                            return G_SOURCE_REMOVE;
                        },
                        self);
                    return G_SOURCE_REMOVE;
                },
                this);
        gtk_main();
        g_source_remove(timer);
    }
};
int gui(const Paths& dirs, Config config, bool smoke) {
    if (!gtk_init_check(nullptr, nullptr))
        throw std::runtime_error(
            "Cannot open the desktop. Use a graphical session or the command line.");
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, nullptr);
    GtkApp app(dirs, config);
    app.run(smoke);
    return 0;
}
void guiError(const std::string& message) {
    if (!gtk_init_check(nullptr, nullptr))
        return;
    auto dialog = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE, "%s", message.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
} // namespace launcher
