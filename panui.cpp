#include <iostream>

#include <SDL2/SDL.h>
#undef main
#include <phoenix/phoenix.hpp>
#define SDL_DYNLIB
#include <mupen/m64p_common.h>
#include <mupen/m64p_frontend.h>
#include <mupen/m64p_types.h>

using namespace nall;
using namespace phoenix;

namespace API
{
    ptr_CoreGetAPIVersions CoreGetAPIVersions;
    ptr_CoreStartup CoreStartup;
    ptr_CoreAttachPlugin CoreAttachPlugin;
    ptr_CoreDetachPlugin CoreDetachPlugin;
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
    unsigned romsize;
}

char working_dir[PATH_MAX];
string romname;
SDL_Thread * corethread;
SDL_Thread * romthread;

void debug (void * ctx, int level, const char * msg)
{
    if ( level != M64MSG_VERBOSE )
        std::cout << (const char *)ctx << ": " << msg << "\n";
}

struct Debugger : Window
{
    FixedLayout layout;
    Button btn_break;
    Debugger();
};

struct MainWindow : Window
{
    FixedLayout layout;
    Button btn_load;
    Button btn_options;
    Button btn_save;
    Button btn_restore;
    Button btn_pauser;
    image img_pause;
    image img_play;
    bool paused;
    BrowserWindow browser;
    MainWindow();
    nall::function<void()> do_play;
    nall::function<void()> do_pause;
    nall::function<void()> do_loadrom;
    nall::function<void()> do_stop;
};

int bootscript( void * ptr )
{
    if(!ptr)
    {
        std::cout << "UI: Bad window in core boot.\n";
        return 0;
    }
    MainWindow * mainwin = (MainWindow *)ptr;
    
    if(romthread)
    {
        std::cout << "UI: Waiting for rom...\n";
        SDL_WaitThread(romthread, NULL); // safe to pass potential NULL (data race) to SDL_WaitThread
        std::cout << "UI: ROM got: " << romname << "\n";
    }
    if(romname.equals(""))
    {
        std::cout << "UI: Bad ROM name, leaving boot script.";
        corethread = NULL;
        return 0;
    }
    m64p_error err = API::CoreDoCommand(M64CMD_ROM_OPEN, API::romsize, API::romdata);
    if(err)
    {
        std::cout << "Error loading ROM: " << err;
        corethread = NULL;
        return 0;
    }
    std::cout << "UI: Did load ROM; attaching plugins.\n";
    
    m64p_plugin_type VideoType;
    int VideoVersion;
    const char * VideoName;
    API::VideoVersion(&VideoType, &VideoVersion, NULL, &VideoName, NULL);
    err = API::CoreAttachPlugin(VideoType, API::Video);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "Video plugin errored while attaching: " << err;
        corethread = NULL;
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
        corethread = NULL;
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
        corethread = NULL;
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
        corethread = NULL;
        return 0;
    }
    std::cout << "UI: Did attach all plugins; running ROM.\n";
    // no more returns until tail of function
    
    mainwin->btn_pauser.setImage(mainwin->img_pause, Orientation::Vertical);
    mainwin->btn_pauser.onActivate = mainwin->do_pause;
    mainwin->btn_load.onActivate = mainwin->do_stop;
    mainwin->btn_load.setText("Stop Emulation");
    mainwin->paused = false;
    
    API::CoreDoCommand(M64CMD_EXECUTE, 0, NULL);
    std::cout << "UI: Emulation ended.\n";
    API::CoreDoCommand(M64CMD_ROM_CLOSE, 0, NULL);
    std::cout << "UI: Did close ROM.\n";
    
    auto err2 = M64ERR_SUCCESS;
    
    err = API::CoreDetachPlugin(RSPType);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "RSP plugin errored while detaching: " << err;
        err2 = err;
    }
    err = API::CoreDetachPlugin(InputType);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "Input plugin errored while detaching: " << err;
        err2 = err;
    }
    err = API::CoreDetachPlugin(AudioType);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "Audio plugin errored while detaching: " << err;
        err2 = err;
    }
    err = API::CoreDetachPlugin(VideoType);
    if(err != M64ERR_SUCCESS)
    {
        std::cout << "Video plugin errored while detaching: " << err;
        err2 = err;
    }
    //m64p_error CoreDoCommand(m64p_command Command, int ParamInt, void *ParamPtr)
    
    if(err2 == M64ERR_SUCCESS)
        std::cout << "UI: Did detach all plugins; cleaning state, closing thread.\n";
    
    mainwin->btn_pauser.onActivate = mainwin->do_play;
    mainwin->btn_load.onActivate = mainwin->do_loadrom;
    mainwin->btn_load.setText("Load ROM");
    corethread = NULL;
    
    std::cout << "UI: Core quit.\n";
    
    return 0;
}

int loadrom (string romname)
{
    if(working_dir)
        chdir((const char *)(working_dir));
    
    if(!romname or romname.equals(""))
    {
        std::cout << "UI: Bad romname, returning.\n";
        corethread = NULL;
        return 0;
    }
    
    char * cstr_romname = romname.data();
    std::cout << "UI: ROM cstring: " << cstr_romname << "\n";
    file romfile(cstr_romname, file::mode::read);
    
    API::romdata = (uint8_t *)malloc(romfile.size());
    API::romsize = romfile.size();
    
    romfile.read(API::romdata, API::romsize);
}

int romscript( void * window )
{
    romname = ((MainWindow *)window)->browser.setParent(*((MainWindow *)window)).setFilters("n64 roms (*.n64,*.z64,*.v64)").open();
    loadrom(romname);
    
    romthread = NULL;
    return 0;
}

MainWindow::MainWindow()
{
    do_play = [this]()
    {
        if(corethread and this->paused)
        {
            API::CoreDoCommand(M64CMD_RESUME, 0, NULL);
            this->btn_pauser.setImage(this->img_pause, Orientation::Vertical);
            this->btn_pauser.onActivate = this->do_pause;
            this->paused = false;
        }
        else
            std::cout << "UI: No corethread in do_play\n";
    };
    do_pause = [this]()
    {
        if(corethread and !this->paused)
        {
            API::CoreDoCommand(M64CMD_PAUSE, 0, NULL);
            this->btn_pauser.setImage(this->img_play, Orientation::Vertical);
            this->btn_pauser.onActivate = this->do_play;
            this->paused = true;
        }
        else
            std::cout << "UI: No corethread in do_play\n";
    };
    do_loadrom  = [this]()
    {
        if(romthread or corethread)
            return;
        
        getcwd(working_dir, PATH_MAX);
        if(!working_dir)
        {
            std::cout << "UI: Error caching current directory.";
            return;
        }

        romthread = SDL_CreateThread(romscript, "RomScript", this);
        //romthread is Wait-ed in corethread
        corethread = SDL_CreateThread(bootscript, "BootScript", this);
        SDL_DetachThread(corethread);
    };
    do_stop = [this]()
    {
        if(corethread)
        {
            API::CoreDoCommand(M64CMD_STOP, 0, NULL);
            API::CoreDoCommand(M64CMD_ROM_CLOSE, 0, NULL);
            m64p_video_mode val = M64VIDEO_NONE;
            API::CoreDoCommand(M64CMD_CORE_STATE_SET, M64CORE_VIDEO_MODE, &val);
            this->btn_load.setText("Load ROM");
        }
        else
            std::cout << "UI: No corethread in do_stop\n";
    };
    setFrameGeometry({64, 64, 242, 128});
    
    paused = true;
    
    browser.setTitle("Load ROM");
    
    btn_load.setText("Load ROM");
    btn_load.onActivate = do_loadrom;
    btn_options.setText("Options");
    
    btn_save.setText("Save");
    btn_save.onActivate = [this]()
    {
        API::CoreDoCommand(M64CMD_STATE_SAVE, 0, NULL);
    };
    btn_restore.setText("Load");
    btn_restore.onActivate = [this]()
    {
        API::CoreDoCommand(M64CMD_STATE_LOAD, 0, NULL);
    };
    
    img_play.load("play.png");
    img_pause.load("pause.png");
    
    btn_pauser.onActivate = do_play;
    btn_pauser.setImage(img_play, Orientation::Vertical);
    
    layout.append(btn_load,    Geometry{10      , 10         , 128 , 24});
    layout.append(btn_options, Geometry{10      , 10+ 24+4   , 128 , 24});
    layout.append(btn_save,    Geometry{10      , 10+(24+4)*2, 64-2, 24});
    layout.append(btn_restore, Geometry{10+64+2 , 10+(24+4)*2, 64-2, 24});
    layout.append(btn_pauser,  Geometry{10+128+4, 10         , 80  , 80});
    append(layout);

    onClose = &Application::quit;
    
    setResizable(false);
    setVisible(); // must be after setResizable()
}

int main(int argc, char *argv[])
{
    // GET SHIT RUNNING (aka everything is currently hardcoded)
    
    // core
    
    romname = "";
    romthread = NULL;
    
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
    
    API::CoreDetachPlugin = (ptr_CoreDetachPlugin)SDL_LoadFunction(core, "CoreDetachPlugin");
    std::cout << SDL_GetError();
    if(!API::CoreDetachPlugin)
        return 0;
    
    API::CoreDoCommand = (ptr_CoreDoCommand)SDL_LoadFunction(core, "CoreDoCommand");
    std::cout << SDL_GetError();
    if(!API::CoreDoCommand)
        return 0;
    
    // Video
    const char * dllname = "";
    if(argc > 1)
        dllname = argv[1];
    else
        dllname = "mupen64plus-video-glide64mk2.dll";
    API::Video = SDL_LoadObject(dllname);
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
    
    std::cout << "UI: Did startup all plugins.\n";
    
    MainWindow * w = new MainWindow;
    
    if(argc > 2)
    {
        std::cout << "UI: Found ROM on command line, loading: " << argv[2] << "\n";
        loadrom(string(argv[2]));
        corethread = SDL_CreateThread(bootscript, "BootScript", w);
    }
    
    Application::run();
    return 0;
}
