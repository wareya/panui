#include <iostream>

#include <SDL2/SDL.h>
#undef main
#include <phoenix/phoenix.hpp>
#define SDL_DYNLIB
#include <mupen/m64p_common.h>
#include <mupen/m64p_frontend.h>
#include <mupen/m64p_types.h>
#include <mupen/m64p_debugger.h>

#include <atomic>

using namespace nall;
using namespace phoenix;

namespace API
{
    ptr_CoreGetAPIVersions CoreGetAPIVersions;
    ptr_CoreStartup CoreStartup;
    ptr_CoreAttachPlugin CoreAttachPlugin;
    ptr_CoreDetachPlugin CoreDetachPlugin;
    ptr_CoreDoCommand CoreDoCommand;
	
    ptr_DebugSetCallbacks DebugSetCallbacks;
    ptr_DebugSetRunState DebugSetRunState;
    ptr_DebugGetState DebugGetState;
	ptr_DebugStep DebugStep;
	ptr_DebugMemGetPointer DebugMemGetPointer;
    ptr_DebugMemRead32 DebugMemRead32;
    
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
    
	template<typename funcptr>
	bool LoadFunction ( funcptr * function, const char * funcname, void * object )
	{
		*function = (funcptr)SDL_LoadFunction(object, funcname);
		if(!function)
		{
			std::cout << "Could not find reference " << SDL_GetError()
					  << " in "
					  << (object == Video?"Video":
						  object == Audio?"Audio":
						  object == RSP  ?"RSP":
					 	  object == Input?"Input":
						  "Core?")
					  << " dynamic library\n";
			return 1;
		}
		return 0;
	}
	
    uint8_t * romdata;
    unsigned romsize;
}

char working_dir[PATH_MAX];
string romname;
std::atomic<SDL_Thread *> corethread;
std::atomic<SDL_Thread *> romthread;

void debug (void * ctx, int level, const char * msg)
{
    if ( level != M64MSG_VERBOSE )
        std::cout << (const char *)ctx << ": " << msg << "\n";
}

struct MainWindow;

struct MemoryWindow : Window
{
	FixedLayout layout;
	TextEdit display;
	LineEdit address;
	
	bool editing;
	Uint32 inputnum;
	MemoryWindow();
};

struct Debugger : Window
{
    FixedLayout layout;
    Button btn_memory;
    Button btn_registers;
    Button btn_search;
    Button btn_commands;
	bool visible;
    MainWindow * parent;
    Debugger(MainWindow * arg_parent);
	MemoryWindow win_memory;
};

struct Options : Window
{
    FixedLayout layout;
    Button btn_apply;
    MainWindow * parent;
    Options(MainWindow * arg_parent);
    unsigned short config_height;
    unsigned short config_width;
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
    Options * win_options;
	Debugger * win_debugger;
    bool paused;
    BrowserWindow browser;
    MainWindow();
    nall::function<void()> do_play;
    nall::function<void()> do_pause;
    nall::function<void()> do_loadrom;
    nall::function<void()> do_stop;
};

MemoryWindow::MemoryWindow()
{
	setTitle("Memory");
	
	auto update = [this]()
	{
		auto size = sizeof(Uint32);
		if(this->address.text().length() <= 8 or (this->address.text().beginsWith("0x") and this->address.text().length() <= 10))
			inputnum = hex(this->address.text());
		else
		{
			puts("UI: Invalid address input in memory viewer.");
			return;
		}
		inputnum = inputnum/size*size;
		
		auto numwide = 4;
		auto sizewide = numwide*size;
		auto sizetall = 0x30;
		
		nall::string displaytext("");
		
		Uint32 buffer;
		for (auto j = 0; j < sizetall*sizewide; j += sizewide)
		{
			displaytext.append("0x", hex<8, '0'>(inputnum+j), ": ");
			for (auto i = 0; i < sizewide; i += size)
			{
				displaytext.append(hex<8, '0'>(API::DebugMemRead32(inputnum+i+j)));
				if(i+size < sizewide)
					displaytext.append(" ");
			}
			if(j+sizewide < sizetall*sizewide)
				displaytext.append("\n");
		}
		display.setText(displaytext);
		ret:
		return;
	};
	auto font = Font::monospace(10);
	display.setFont(font);
	address.setFont(font);
	address.setText("0x80000000");
	address.onChange = update;
	
	auto prefixsize = Font::size(font, "0x");
	auto delimsize = Font::size(font, ": ");
	auto wordsize = Font::size(font, "12345678");
	auto chunksize = Font::size(font, "12345678 12345678 12345678 12345678");
	
	auto viewwidth = prefixsize.width+wordsize.width+delimsize.width+chunksize.width+6+24;
	auto entrywidth = wordsize.width+prefixsize.width+6;
	auto entryheight = wordsize.height+6;
	auto viewheight = wordsize.height*0x30+6;
	
	setGeometry({64, 256, viewwidth+20, entryheight + 30 + viewheight});
	
	layout.append(address, Geometry{10, 10, entrywidth, entryheight});
	layout.append(display, Geometry{10, entryheight+20, viewwidth, viewheight});
	append(layout);
	
	
	setVisible(false);
}

Options::Options(MainWindow * arg_parent)
{
	setTitle("Options");
    parent = arg_parent;
    setGeometry({64, 256, 340, 500});
    config_height = 960;
    config_width = 720;
    btn_apply.setText("Apply");
    btn_apply.onActivate = [this]()
    {
        std::cout << "UI: Options: Applying options-- \n";
        m64p_2d_size val = {this->config_height, this->config_width};
        //std::cout << "UI: Options: Setting resolution to " << std::dec << this->config_height << " " << this->config_width << " (" << std::hex << val << ")\n";
        auto err = API::CoreDoCommand(M64CMD_CORE_STATE_SET, M64CORE_VIDEO_SIZE, &val);
        if(err != M64ERR_SUCCESS)
            std::cout << "UI: Options: Core returned error on attempt to change video size: " << err << "\n";
        
    };
    layout.append(btn_apply, Geometry{10, 10, 40, 24});
    append(layout);
    setResizable(false);
    setVisible(false); // must be after setResizable()
    onClose = [this]()
    {
        this->setVisible(!this->visible());
    };
}

Debugger::Debugger(MainWindow * arg_parent)
{
	setTitle("Debugger");
    parent = arg_parent;
    setGeometry({64, 64, 20+128+4, 10+24+4+24+10});
	btn_memory.setText("Memory");
	btn_memory.onActivate = [this]()
	{
		//this->btn_memory.setVisible(!this->btn_memory.visible());
        this->win_memory.setVisible(true);
	};
	btn_registers.setText("Registers");
	btn_registers.onActivate = [this]()
	{
		//this->btn_registers.setVisible(!this->btn_registers.visible());
	};
	btn_search.setText("Search");
	btn_search.onActivate = [this]()
	{
		//this->btn_search.setVisible(!this->btn_search.visible());
	};
	btn_commands.setText("Commands");
	btn_commands.onActivate = [this]()
	{
		//this->btn_commands.setVisible(!this->btn_commands.visible());
	};
	
    layout.append(btn_memory,    Geometry{10     , 10     , 64-2, 24});
    layout.append(btn_registers, Geometry{10+64+2, 10     , 64-2, 24});
    layout.append(btn_search,    Geometry{10     , 10+24+4, 64-2, 24});
    layout.append(btn_commands,  Geometry{10+64+2, 10+24+4, 64-2, 24});
	
    onClose = [this]()
	{
		this->parent->win_debugger = NULL;
	};
    
    append(layout);
    setResizable(false);
    setVisible(false); // must be after setResizable()
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

int subbootscript( void * ptr )
{
	std::cout << "UI: Started core threads -- waiting on full load.\n";
	while(romthread)
		SDL_Delay(10);
	
	auto time = SDL_GetTicks();
    while(API::CoreDoCommand(M64CMD_RESUME, 0, NULL) != M64ERR_SUCCESS and SDL_GetTicks()-time < 2000)
		SDL_Delay(10);
	if(SDL_GetTicks()-time >= 2000)
		std::cout << "UI: Core locked up for two seconds, you're on your own.\n";
	
	if(API::DebugSetRunState(2) == M64ERR_SUCCESS)
	{
		std::cout << "UI: Core seemed to allow setting debug run state.\n";
		if(API::DebugStep() != M64ERR_SUCCESS)
			std::cout << "UI: Failed to finish resuming emulation.\n";
		else
		{
			std::cout << "UI: Finished loading ROM and initializing emulator.\n";
			((MainWindow*)ptr)->win_debugger->setVisible(true);
			((MainWindow*)ptr)->win_debugger->visible = true;
		}
	}
	else
		std::cout << "UI: Core does not seem to be built with debugging enabled.\n";
	
	return 0;
}

int loadrom (string arg_romname)
{
    if(working_dir)
        chdir((const char *)(working_dir));
    
    if(!arg_romname or arg_romname.equals(""))
    {
        std::cout << "UI: Bad romname, returning.\n";
        corethread = NULL;
        return 0;
    }
    romname = arg_romname;
    
    char * cstr_romname = arg_romname.data();
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
	setTitle("Panui");
    win_options = new Options(this);
    win_debugger = new Debugger(this);
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
		
		auto startthread = SDL_CreateThread(subbootscript, "SubBootScript", this);
        SDL_DetachThread(startthread);
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
			if(win_debugger)
				delete win_debugger;
        }
        else
            std::cout << "UI: No corethread in do_stop\n";
    };
    setGeometry({64, 64, 10+128+4+80+10, 10+24*3+4*2+10});
    
    paused = true;
    
    browser.setTitle("Load ROM");
    
    btn_load.setText("Load ROM");
    btn_load.onActivate = do_loadrom;
    
    btn_options.setText("Options");
    btn_options.onActivate = [this]()
    {
        this->win_options->setVisible(!this->win_options->visible());
    };
    
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
    
	if(API::LoadFunction<ptr_CoreGetAPIVersions>(&API::CoreGetAPIVersions, "CoreGetAPIVersions", core))
		return 0;
	
    int VersionConfig, VersionDebug, VersionVidext, VersionExtra;
    API::CoreGetAPIVersions(&VersionConfig, &VersionDebug, &VersionVidext, &VersionExtra);
    
	if(API::LoadFunction<ptr_CoreStartup>(&API::CoreStartup, "CoreStartup", core))
        return 0;
    
    API::CoreStartup(0x020000, ".", NULL, (void *)"Core", &debug, NULL, NULL);
    
    if(API::LoadFunction<ptr_CoreAttachPlugin>(&API::CoreAttachPlugin, "CoreAttachPlugin", core))
        return 0;
    
    if(API::LoadFunction<ptr_CoreDetachPlugin>(&API::CoreDetachPlugin, "CoreDetachPlugin", core))
        return 0;
    
    if(API::LoadFunction<ptr_CoreDoCommand>(&API::CoreDoCommand, "CoreDoCommand", core))
        return 0;
        
    if(API::LoadFunction<ptr_DebugSetCallbacks>(&API::DebugSetCallbacks, "DebugSetCallbacks", core))
        return 0;
    API::DebugSetCallbacks(NULL, NULL, NULL);
	
    if(API::LoadFunction<ptr_DebugSetRunState>(&API::DebugSetRunState, "DebugSetRunState", core))
        return 0;
    if(API::LoadFunction<ptr_DebugGetState>(&API::DebugGetState, "DebugGetState", core))
        return 0;
    if(API::LoadFunction<ptr_DebugStep>(&API::DebugStep, "DebugStep", core))
        return 0;
    if(API::LoadFunction<ptr_DebugMemGetPointer>(&API::DebugMemGetPointer, "DebugMemGetPointer", core))
        return 0;
    if(API::LoadFunction<ptr_DebugMemRead32>(&API::DebugMemRead32, "DebugMemRead32", core))
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
    
    if(API::LoadFunction<ptr_PluginStartup>(&API::VideoStartup, "PluginStartup", API::Video))
    {
        std::cout << "Video plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    if(API::LoadFunction<ptr_PluginGetVersion>(&API::VideoVersion, "PluginGetVersion", API::Video))
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
    
    if(API::LoadFunction<ptr_PluginStartup>(&API::AudioStartup, "PluginStartup", API::Audio))
    {
        std::cout << "Audio plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    if(API::LoadFunction<ptr_PluginGetVersion>(&API::AudioVersion, "PluginGetVersion", API::Audio))
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
    
    if(API::LoadFunction<ptr_PluginStartup>(&API::InputStartup, "PluginStartup", API::Input))
    {
        std::cout << "Input plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    if(API::LoadFunction<ptr_PluginGetVersion>(&API::InputVersion, "PluginGetVersion", API::Input))
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
    
    if(API::LoadFunction<ptr_PluginStartup>(&API::RSPStartup, "PluginStartup", API::RSP))
    {
        std::cout << "RSP plugin is not a valid m64p plugin (no startup).";
        return 0;
    }
    
    if(API::LoadFunction<ptr_PluginGetVersion>(&API::RSPVersion, "PluginGetVersion", API::RSP))
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
