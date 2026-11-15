#include "gui.hpp"
#include <gtk/gtk.h>
#include <array>
#include <functional>

namespace launcher {
class GtkApp {
    GuiModel model;
    GtkWidget *window=nullptr,*game=nullptr,*proton=nullptr,*runtime=nullptr,*scale=nullptr,*graphics=nullptr;
    GtkWidget *controllers=nullptr,*taa=nullptr,*status=nullptr,*resolution=nullptr;
    std::vector<GtkWidget*> actions;
    std::vector<double> scales{.25,.5,.625,.75,1,1.25,1.5};
    void error(const std::string& message) {
        model.status=message;gtk_label_set_text(GTK_LABEL(status),message.c_str());
        auto dialog=gtk_message_dialog_new(GTK_WINDOW(window),GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,"%s",message.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);
    }
    void attempt(const std::function<void()>& fn) {try {fn();}catch(const std::exception& e) {error(e.what());}}
    Config preferences() {
        Config c=model.config;c.game=gtk_entry_get_text(GTK_ENTRY(game));c.proton=gtk_entry_get_text(GTK_ENTRY(proton));c.runtime=gtk_entry_get_text(GTK_ENTRY(runtime));
        c.scale=scales.at((size_t)gtk_combo_box_get_active(GTK_COMBO_BOX(scale)));
        const char* id=gtk_combo_box_get_active_id(GTK_COMBO_BOX(graphics));c.graphics=id?id:"auto";
        c.controllers=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controllers));c.antialiasing=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(taa));return c;
    }
    void refresh() {
        gtk_label_set_text(GTK_LABEL(status),model.status.c_str());
        const auto label=model.resolution(scales.at((size_t)gtk_combo_box_get_active(GTK_COMBO_BOX(scale))));
        gtk_label_set_text(GTK_LABEL(resolution),label.c_str());
        const bool available=model.available();for(auto button:actions)gtk_widget_set_sensitive(button,available);
    }
    static GtkWidget* label(const char* text) {auto w=gtk_label_new(text);gtk_label_set_xalign(GTK_LABEL(w),0);gtk_label_set_line_wrap(GTK_LABEL(w),TRUE);return w;}
    GtkWidget* field(GtkWidget* box,const char* title,const std::string& value,bool directory) {
        gtk_box_pack_start(GTK_BOX(box),label(title),FALSE,FALSE,0);
        auto row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
        auto entry=gtk_entry_new();gtk_entry_set_text(GTK_ENTRY(entry),value.c_str());gtk_widget_set_hexpand(entry,TRUE);
        gtk_box_pack_start(GTK_BOX(row),entry,TRUE,TRUE,0);auto browse=gtk_button_new_with_label("Browse...");gtk_box_pack_start(GTK_BOX(row),browse,FALSE,FALSE,0);
        struct Browse {GtkApp* app;GtkWidget* entry;bool directory;};auto data=new Browse{this,entry,directory};
        g_signal_connect_data(browse,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p) {
            auto& d=*static_cast<Browse*>(p);auto chooser=gtk_file_chooser_native_new("Select a path",GTK_WINDOW(d.app->window),
                d.directory?GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER:GTK_FILE_CHOOSER_ACTION_OPEN,"Select","Cancel");
            const char* current=gtk_entry_get_text(GTK_ENTRY(d.entry));if(*current)gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser),current);
            if(gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) {char* file=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));if(file)gtk_entry_set_text(GTK_ENTRY(d.entry),file);g_free(file);}
            g_object_unref(chooser);
        }),data,+[](gpointer p,GClosure*) {delete static_cast<Browse*>(p);},G_CONNECT_DEFAULT);
        return entry;
    }
    void button(GtkWidget* box,const char* title,const char* command) {
        auto w=gtk_button_new_with_label(title);gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,0);actions.push_back(w);
        g_object_set_data(G_OBJECT(w),"command",(gpointer)command);
        g_signal_connect(w,"clicked",G_CALLBACK(+[](GtkButton* b,gpointer data) {
            auto self=static_cast<GtkApp*>(data);self->attempt([&] {self->model.start((const char*)g_object_get_data(G_OBJECT(b),"command"),self->preferences());self->refresh();});
        }),this);
    }
public:
    GtkApp(const Paths& dirs,Config config):model(dirs,config) {
        window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),"Dirt Town VR Mod");gtk_window_set_default_size(GTK_WINDOW(window),740,-1);
        g_signal_connect(window,"destroy",G_CALLBACK(+[](GtkWidget*,gpointer) {gtk_main_quit();}),nullptr);
        auto box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);gtk_container_set_border_width(GTK_CONTAINER(box),22);gtk_container_add(GTK_CONTAINER(window),box);
        auto title=label("");gtk_label_set_markup(GTK_LABEL(title),"<span size='xx-large' weight='bold'>Dirt Town VR</span>");gtk_box_pack_start(GTK_BOX(box),title,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(box),label("OpenXR | Demo build 26658761"),FALSE,FALSE,0);
        game=field(box,"Game installation",config.game,true);runtime=field(box,"OpenXR runtime manifest (blank = active runtime)",config.runtime,false);
        proton=field(box,"Proton executable (blank = detect)",config.proton,false);
        auto frame=gtk_frame_new("Rendering and input");gtk_box_pack_start(GTK_BOX(box),frame,FALSE,FALSE,8);
        auto options=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);gtk_container_set_border_width(GTK_CONTAINER(options),12);gtk_container_add(GTK_CONTAINER(frame),options);
        auto row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);gtk_box_pack_start(GTK_BOX(options),row,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(row),label("Resolution:"),FALSE,FALSE,0);scale=gtk_combo_box_text_new();
        if(std::find(scales.begin(),scales.end(),config.scale)==scales.end())scales.push_back(config.scale);
        int selected=0;for(size_t i=0;i<scales.size();++i) {auto s=std::to_string(scales[i]*100);s.erase(s.find_last_not_of('0')+1);if(s.back()=='.')s.pop_back();s+='%';gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scale),s.c_str());if(scales[i]==config.scale)selected=(int)i;}
        gtk_combo_box_set_active(GTK_COMBO_BOX(scale),selected);gtk_box_pack_start(GTK_BOX(row),scale,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(row),label("Graphics:"),FALSE,FALSE,0);graphics=gtk_combo_box_text_new();
        for(const char* value:{"auto","d3d12","d3d11"})gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(graphics),value,value);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(graphics),config.graphics.c_str());gtk_box_pack_start(GTK_BOX(row),graphics,FALSE,FALSE,0);
        resolution=label("");gtk_box_pack_start(GTK_BOX(options),resolution,FALSE,FALSE,0);
        g_signal_connect(scale,"changed",G_CALLBACK(+[](GtkComboBox*,gpointer p) {auto self=static_cast<GtkApp*>(p);self->attempt([&] {self->refresh();});}),this);
        controllers=gtk_check_button_new_with_label("Use VR controllers");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controllers),config.controllers);gtk_box_pack_start(GTK_BOX(options),controllers,FALSE,FALSE,0);
        taa=gtk_check_button_new_with_label("Temporal antialiasing");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(taa),config.antialiasing);gtk_box_pack_start(GTK_BOX(options),taa,FALSE,FALSE,0);
        auto edit=gtk_button_new_with_label("Controller settings...");gtk_widget_set_halign(edit,GTK_ALIGN_START);gtk_box_pack_start(GTK_BOX(options),edit,FALSE,FALSE,0);
        g_signal_connect(edit,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p) {auto self=static_cast<GtkApp*>(p);self->attempt([&] {openPath(self->model.dirs.config/"controls.ini");});}),this);
        status=label(model.status.c_str());gtk_label_set_max_width_chars(GTK_LABEL(status),80);gtk_box_pack_start(GTK_BOX(box),status,FALSE,FALSE,0);
        row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
        button(row,"Detect headset","runtime");button(row,"Launch VR","launch");button(row,"Test headset","probe");button(row,"GPU self-test","gpu-test");
        gtk_box_pack_start(GTK_BOX(box),label("Load a site on the monitor. F9: recenter | F10: VR on/off | F11: antialiasing\nRight stick: look while walking, implement control in dozer/loader.\nMenus and HUD remain on the monitor."),FALSE,FALSE,8);
        auto logs=gtk_button_new_with_label("Open logs");gtk_widget_set_halign(logs,GTK_ALIGN_START);gtk_box_pack_start(GTK_BOX(box),logs,FALSE,FALSE,0);
        g_signal_connect(logs,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p) {auto self=static_cast<GtkApp*>(p);self->attempt([&] {openPath(self->model.dirs.logs);});}),this);
        auto licences=gtk_button_new_with_label("Licences");gtk_widget_set_halign(licences,GTK_ALIGN_START);gtk_box_pack_start(GTK_BOX(box),licences,FALSE,FALSE,0);
        g_signal_connect(licences,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p) {auto self=static_cast<GtkApp*>(p);self->attempt([&] {openPath(self->model.dirs.cache/"licenses");});}),this);
        refresh();gtk_widget_show_all(window);
    }
    void run(bool smoke) {
        auto timer=g_timeout_add(500,+[](gpointer p)->gboolean {auto self=static_cast<GtkApp*>(p);self->attempt([&] {self->model.poll();self->refresh();});return G_SOURCE_CONTINUE;},this);
        if(smoke)g_timeout_add(1000,+[](gpointer p)->gboolean {
            auto self=static_cast<GtkApp*>(p);
            auto surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,gtk_widget_get_allocated_width(self->window),gtk_widget_get_allocated_height(self->window));
            auto cr=cairo_create(surface);gtk_widget_draw(self->window,cr);cairo_destroy(cr);
            cairo_surface_write_to_png(surface,text(self->model.dirs.logs/"launcher-preview.png").c_str());cairo_surface_destroy(surface);
            gtk_widget_destroy(self->window);return G_SOURCE_REMOVE;
        },this);
        gtk_main();g_source_remove(timer);
    }
};
int gui(const Paths& dirs,Config config,bool smoke) {
    if(!gtk_init_check(nullptr,nullptr))throw std::runtime_error("Cannot open the desktop. Use a graphical session or the command line.");
    GtkApp app(dirs,config);app.run(smoke);return 0;
}
void guiError(const std::string& message) {
    if(!gtk_init_check(nullptr,nullptr))return;
    auto dialog=gtk_message_dialog_new(nullptr,GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,"%s",message.c_str());gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);
}
} // namespace launcher
