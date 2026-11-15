#include "core.hpp"
#include "gui.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace launcher {
int main(const std::vector<std::string>& args) {
    fs::path report;bool graphical=args.empty(),smoke=false,diagnostic=false;std::string command="gui";Json overrides=Json::object();
    try {
        for(size_t i=0;i<args.size();++i) {
            const auto& a=args[i];auto next=[&]()->std::string {if(i+1>=args.size())throw std::runtime_error("Missing value after "+a);return args[++i];};
            if(a=="--help" || a=="-h") {
                std::cout<<"Dirt Town VR "<<DTVR_VERSION<<"\nUsage: dirttown-vr [gui|launch|runtime|probe|gpu-test|core-test|eye-test|prepare|self-test]\n"
                    "  --game FOLDER --proton FILE --runtime MANIFEST --resolution-scale 0.25-1.5\n"
                    "  --graphics auto|d3d12|d3d11 --no-controllers --no-taa --diagnostic\n";return 0;
            }
            if(a=="--game")overrides["game"]=next();else if(a=="--proton")overrides["proton"]=next();else if(a=="--runtime")overrides["runtime"]=next();
            else if(a=="--resolution-scale") {const auto value=next();size_t end=0;const double scale=std::stod(value,&end);if(end!=value.size() || !std::isfinite(scale))throw std::runtime_error("Invalid resolution scale");overrides["scale"]=scale;}
            else if(a=="--graphics")overrides["graphics"]=next();else if(a=="--no-controllers")overrides["controllers"]=false;else if(a=="--no-taa")overrides["antialiasing"]=false;
            else if(a=="--diagnostic")diagnostic=true;else if(a=="--smoke-ui") {smoke=true;graphical=true;}
            else if(a=="--report")report=path(next());else if(a=="gui" || a=="launch" || a=="runtime" || a=="probe" || a=="gpu-test" || a=="core-test" || a=="eye-test" || a=="prepare" || a=="self-test")command=a;
            else throw std::runtime_error("Unknown argument: "+a);
        }
        graphical=command=="gui";
        if(command=="self-test") {selfTest();if(!report.empty())writeFile(report,Json({{"checks","passed"}}).dump());std::cout<<"PASS: native launcher checks\n";return 0;}
        const auto dirs=prepare();auto config=Config::load(dirs);
        if(!fs::exists(dirs.config/"config.json"))config.save(dirs);
        auto values=config.json();values.update(overrides);config=Config::fromJson(values);
        if(graphical)return gui(dirs,config,smoke);
        Json result;int code=0;
        if(command=="prepare")result={{"config",text(dirs.config)},{"cache",text(dirs.cache)},{"logs",text(dirs.logs)},{"settings",config.json()}};
        else if(command=="runtime")result=inspectRuntime(dirs,runtimeEnvironment(config,dirs));
        else {code=run(config,dirs,command,diagnostic);result={{"exit_code",code}};}
        if(!report.empty())writeFile(report,result.dump(2)+'\n');std::cout<<result.dump(2)<<std::endl;return code;
    } catch(const std::exception& e) {
        if(!report.empty())try {writeFile(report,Json({{"error",e.what()}}).dump(2)+'\n');}catch(...) {}
        std::cerr<<e.what()<<std::endl;if(graphical)guiError(e.what());return 1;
    }
}
}
#ifdef _WIN32
static void connectConsole() {
    if(!GetStdHandle(STD_OUTPUT_HANDLE))AttachConsole(ATTACH_PARENT_PROCESS);
    const DWORD channels[]={STD_OUTPUT_HANDLE,STD_ERROR_HANDLE};FILE* streams[]={stdout,stderr};
    for(unsigned i=0;i<2;++i) {
        HANDLE original=GetStdHandle(channels[i]),copy=nullptr;
        if(!original || original==INVALID_HANDLE_VALUE || !DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS))continue;
        int fd=_open_osfhandle((intptr_t)copy,_O_TEXT);if(fd<0) {CloseHandle(copy);continue;}
        if(_wfreopen(L"NUL",L"w",streams[i])) {_dup2(fd,_fileno(streams[i]));setvbuf(streams[i],nullptr,_IONBF,0);}
        _close(fd);
    }
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);std::vector<std::string> args;
    if(count>1)connectConsole();
    for(int i=1;i<count;++i)args.push_back(launcher::utf8(argv[i]));LocalFree(argv);return launcher::main(args);
}
#else
int main(int argc,char** argv) {std::vector<std::string> args(argv+1,argv+argc);return launcher::main(args);}
#endif
