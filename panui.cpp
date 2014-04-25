#define SDL_DYNLIB
#include <iostream>
#include <SDL2/SDL.h>
#include <phoenix/phoenix.hpp>
#include <mupen/m64p_common.h>
#include <mupen/m64p_frontend.h>
#include <mupen/m64p_types.h>
#undef main

using namespace nall;
using namespace phoenix;

namespace API
{
    ptr_CoreGetAPIVersions CoreGetAPIVersions;
    ptr_CoreStartup CoreStartup;
    ptr_CoreAttachPlugin CoreAttachPlugin;
    ptr_CoreDoCommand CoreDoCommand;
    
    void * Video;
    ptr_PluginGetVersion VideoVersion; 
    ptr_PluginStartup VideoStartup; 
    
    void * Audio;
    ptr_PluginGetVersion AudioVersion; 
    ptr_PluginStartup AudioStartup; 
    
    void * RSP;
    ptr_PluginGetVersion RSPVersion; 
    ptr_PluginStartup RSPStartup; 
    
    void * Input;
    ptr_PluginGetVersion InputVersion; 
    ptr_PluginStartup InputStartup;
    
    uint8_t * romdata;
}


void debug (void * ctx, int level, const char * msg)
{
    printf("%s: %s\n", (const char *) ctx, msg);
}

int bootscript(void * ptr)
{
    auto romname = (string *)ptr;
    file romfile(*romname, file::mode::read);
    
    API::romdata = (uint8_t *)malloc(romfile.size());
    romfile.read(API::romdata, romfile.size());
    
    m64p_error err = API::CoreDoCommand(M64CMD_ROM_OPEN, romfile.size(), API::romdata);
    if(err)
    {
        std::cout << "Error loading ROM: " << err;
        return 0;
    }
    
    m64p_plugin_type VideoType;
    int VideoVersion;
    const char * VideoName;
    API::VideoVersion(&VideoType, &VideoVersion, NULL, &VideoName, NULL);
    err = API::CoreAttachPlugin(VideoType, API::Video);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "Video plugin errored while attaching: " << err;
        return 0;
    }
    
    m64p_plugin_type AudioType;
    int AudioVersion;
    const char * AudioName;
    API::AudioVersion(&AudioType, &AudioVersion, NULL, &AudioName, NULL);
    err = API::CoreAttachPlugin(AudioType, API::Audio);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "Audio plugin errored while attaching: " << err;
        return 0;
    }
    
    m64p_plugin_type InputType;
    int InputVersion;
    const char * InputName;
    API::InputVersion(&InputType, &InputVersion, NULL, &InputName, NULL);
    err = API::CoreAttachPlugin(InputType, API::Input);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "Input plugin errored while attaching: " << err;
        return 0;
    }
    
    m64p_plugin_type RSPType;
    int RSPVersion;
    const char * RSPName;
    API::RSPVersion(&RSPType, &RSPVersion, NULL, &RSPName, NULL);
    err = API::CoreAttachPlugin(RSPType, API::RSP);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "RSP plugin errored while attaching: " << err;
        return 0;
    }
    
    API::CoreDoCommand(M64CMD_EXECUTE, 0, NULL);
}

string romname;

struct MainWindow : Window
{
    FixedLayout layout;
    Button btnload;
    Button btnoptions;
    Button btnsave;
    Button btnrestore;
    Button btnpauser;
    image pause;
    image play;
    BrowserWindow browser;
    SDL_Thread * corethread;
    
    MainWindow()
    {
        setFrameGeometry({64, 64, 242, 128});
        
        browser.setTitle("Load ROM");
        
        btnload.setText   ("Load ROM");
        btnload.onActivate = [this]()
        {
            romname = browser.setParent(*this).setFilters("n64 roms (*.n64,*.z64)").open();
            corethread = SDL_CreateThread(bootscript, "BootScript", (void *)&romname);
        };
        btnoptions.setText("Options");
        
        btnsave.setText   ("Save");
        btnrestore.setText("Load");
        
        play.load("play.png");
        pause.load("pause.png");
        
        btnpauser.setImage(play, Orientation::Vertical);
        
        layout.append(btnload,    Geometry{10      , 10         , 128 , 24});
        layout.append(btnoptions, Geometry{10      , 10+ 24+4   , 128 , 24});
        layout.append(btnsave,    Geometry{10      , 10+(24+4)*2, 64-2, 24});
        layout.append(btnrestore, Geometry{10+64+2 , 10+(24+4)*2, 64-2, 24});
        layout.append(btnpauser,  Geometry{10+128+4, 10         , 80  , 80});
        append(layout);

        onClose = &Application::quit;
        
        setResizable(false);
        setVisible(); // must be after setResizable()
    }
};

int main()
{
    // GET SHIT RUNNING (aka everything is currently hardcoded)
    
    // core
    
    void * core = SDL_LoadObject("mupen64plus.dll");
    std::cout << SDL_GetError();
    if(!core)
        return 0;
    
    API::CoreGetAPIVersions = (ptr_CoreGetAPIVersions)SDL_LoadFunction(core, "CoreGetAPIVersions");
    std::cout << SDL_GetError();
    if(!API::CoreGetAPIVersions)
        return 0;
    
    int VersionConfig, VersionDebug, VersionVidext, VersionExtra;
    API::CoreGetAPIVersions(&VersionConfig, &VersionDebug, &VersionVidext, &VersionExtra);
    
    API::CoreStartup = (ptr_CoreStartup)SDL_LoadFunction(core, "CoreStartup");
    std::cout << SDL_GetError();
    if(!API::CoreStartup)
        return 0;
    
    API::CoreStartup(0x020000, NULL, NULL, (void *)"Core", &debug, NULL, NULL);
    
    API::CoreAttachPlugin = (ptr_CoreAttachPlugin)SDL_LoadFunction(core, "CoreAttachPlugin");
    std::cout << SDL_GetError();
    if(!API::CoreAttachPlugin)
        return 0;
        
    API::CoreDoCommand = (ptr_CoreDoCommand)SDL_LoadFunction(core, "CoreDoCommand");
    std::cout << SDL_GetError();
    if(!API::CoreDoCommand)
        return 0;
    
    // Video
    
    API::Video = SDL_LoadObject("mupen64plus-video-glide64mk2.dll");
    std::cout << SDL_GetError();
    if(!API::Video)
        return 0;
    
    API::VideoStartup = (ptr_PluginStartup)SDL_LoadFunction(API::Video, "PluginStartup");
    std::cout << SDL_GetError();
    if(!API::VideoStartup)
    {
        std::cout << "Video plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    API::VideoVersion = (ptr_PluginGetVersion)SDL_LoadFunction(API::Video, "PluginGetVersion");
    std::cout << SDL_GetError();
    if(!API::VideoVersion)
    {
        std::cout << "Video plugin is not a valid m64p plugin (no version).";
        return 0;
    }
    
    m64p_error err = API::VideoStartup(core, (void *)"Video", &debug);
    if(err)
    {
        std::cout << "Video plugin errored while starting up: " << err;
        return 0;
    }
    
    // Audio
    
    API::Audio = SDL_LoadObject("mupen64plus-audio-sdl.dll");
    std::cout << SDL_GetError();
    if(!API::Audio)
        return 0;
    
    API::AudioStartup = (ptr_PluginStartup)SDL_LoadFunction(API::Audio, "PluginStartup");
    std::cout << SDL_GetError();
    if(!API::AudioStartup)
    {
        std::cout << "Audio plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    API::AudioVersion = (ptr_PluginGetVersion)SDL_LoadFunction(API::Audio, "PluginGetVersion");
    std::cout << SDL_GetError();
    if(!API::AudioVersion)
    {
        std::cout << "Audio plugin is not a valid m64p plugin (no version).";
        return 0;
    }
    
    err = API::AudioStartup(core, (void *)"Audio", &debug);
    if(err)
    {
        std::cout << "Audio plugin errored while starting up: " << err;
        return 0;
    }
    
    // Input
    
    API::Input = SDL_LoadObject("mupen64plus-Input-sdl.dll");
    std::cout << SDL_GetError();
    if(!API::Input)
        return 0;
    
    API::InputStartup = (ptr_PluginStartup)SDL_LoadFunction(API::Input, "PluginStartup");
    std::cout << SDL_GetError();
    if(!API::InputStartup)
    {
        std::cout << "Input plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    API::InputVersion = (ptr_PluginGetVersion)SDL_LoadFunction(API::Input, "PluginGetVersion");
    std::cout << SDL_GetError();
    if(!API::InputVersion)
    {
        std::cout << "Input plugin is not a valid m64p plugin (no version).";
        return 0;
    }
    
    
    err = API::InputStartup(core, (void *)"Input", &debug);
    if(err)
    {
        std::cout << "Input plugin errored while starting up: " << err;
        return 0;
    }
    
    // RSP
    
    API::RSP = SDL_LoadObject("mupen64plus-rsp-hle.dll");
    std::cout << SDL_GetError();
    if(!API::RSP)
        return 0;
    
    API::RSPStartup = (ptr_PluginStartup)SDL_LoadFunction(API::RSP, "PluginStartup");
    std::cout << SDL_GetError();
    if(!API::RSPStartup)
    {
        std::cout << "RSP plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    API::RSPVersion = (ptr_PluginGetVersion)SDL_LoadFunction(API::RSP, "PluginGetVersion");
    std::cout << SDL_GetError();
    if(!API::RSPVersion)
    {
        std::cout << "RSP plugin is not a valid m64p plugin (no version).";
        return 0;
    }
    
    err = API::RSPStartup(core, (void *)"RSP", &debug);
    if(err)
    {
        std::cout << "RSP plugin errored while starting up: " << err;
        return 0;
    }
    
    // window
    
    new MainWindow;
    Application::run();
    return 0;
}
