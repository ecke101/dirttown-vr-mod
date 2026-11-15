#pragma once
#include "core.hpp"
#include <memory>
#include <cmath>
namespace launcher {
struct GuiModel {
    Paths dirs;
    Config config;
    Json headset;
    std::string status="Start your OpenXR runtime and connect the headset.";
    std::unique_ptr<Process> pending;
    fs::path report;
    std::string action;
    GuiModel(const Paths& paths,Config settings):dirs(paths),config(settings),report(dirs.logs/("gui-report-"+std::to_string(processId())+".json")) {}
    bool available() const {return !pending && !gameRunning();}
    void start(const std::string& command,Config settings) {
        if(!available())throw std::runtime_error("Quit the running game or test first.");
        settings.save(dirs);config=settings;std::error_code error;fs::remove(report,error);
        auto child=std::make_unique<Process>();
        std::vector<std::string> args={text(executablePath()),command,"--report",text(report),
            "--game",config.game,"--proton",config.proton,"--runtime",config.runtime,
            "--resolution-scale",std::to_string(config.scale),"--graphics",config.graphics};
        if(!config.controllers)args.push_back("--no-controllers");if(!config.antialiasing)args.push_back("--no-taa");
        child->start(args,environment(),{},dirs.logs/"gui-worker.log");pending=std::move(child);action=command;
        status=command=="runtime"?"Checking the headset...":"Running. Load a site on the monitor after the game opens.";
    }
    bool poll() {
        if(!pending)return false;auto code=pending->poll();if(!code)return false;pending.reset();
        Json result;if(fs::is_regular_file(report))result=Json::parse(readFile(report));
        if(result.contains("error"))throw std::runtime_error(result["error"].get<std::string>());
        if(*code)throw std::runtime_error("Exited with code "+std::to_string(*code)+". Details: "+text(dirs.logs/"gui-worker.log"));
        if(action=="runtime") {
            if(!result.contains("runtime"))throw std::runtime_error("No headset result returned. See launcher logs.");
            headset=result;status=result.at("runtime").get<std::string>()+" | "+result.at("headset").get<std::string>();
        } else status="Exited normally. Logs: "+text(dirs.logs);
        return true;
    }
    std::string resolution(double scale) const {
        if(headset.empty())return "Resolution follows the headset recommendation.";
        std::string result="Per-eye target: ";
        for(size_t i=0;i<2;++i) {if(i)result+=" / ";
            const auto& e=headset.at("eyes").at(i);result+=std::to_string((int)std::lround(e.at(0).get<double>()*scale))+" x "+std::to_string((int)std::lround(e.at(1).get<double>()*scale));}
        return result;
    }
};
void guiError(const std::string& message);
} // namespace launcher
