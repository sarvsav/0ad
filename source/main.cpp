#include "precompiled.h"

#ifdef SCED
# include "ui/StdAfx.h"
# undef ERROR
#endif // SCED

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <stdarg.h>

#include "lib.h"
#include "lib/sdl.h"
#include "lib/ogl.h"
#include "lib/detect.h"
#include "lib/timer.h"
#include "lib/input.h"
#include "lib/res/res.h"
#include "lib/res/tex.h"
#include "lib/res/snd.h"
#include "lib/res/hotload.h"
#include "lib/res/cursor.h"

#include "ps/Profile.h"
#include "ps/ProfileViewer.h"
#include "ps/Loader.h"
#include "ps/Font.h"
#include "ps/CConsole.h"
#include "ps/Game.h"
#include "ps/Interact.h"
#include "ps/Hotkey.h"
#include "ps/ConfigDB.h"
#include "ps/CLogger.h"
#include "ps/i18n.h"
#include "ps/Overlay.h"
#include "ps/StringConvert.h"

#include "graphics/MapReader.h"
#include "graphics/Terrain.h"
#include "graphics/TextureManager.h"
#include "graphics/ObjectManager.h"
#include "graphics/SkeletonAnimManager.h"
#include "graphics/LightEnv.h"
#include "graphics/Model.h"
#include "graphics/UnitManager.h"
#include "graphics/MaterialManager.h"
#include "graphics/MeshManager.h"
#include "renderer/Renderer.h"

#include "simulation/BaseEntityCollection.h"
#include "simulation/Entity.h"
#include "simulation/EntityHandles.h"
#include "simulation/EntityManager.h"
#include "simulation/PathfindEngine.h"
#include "simulation/Scheduler.h"
#include "simulation/Projectile.h"

#include "scripting/ScriptingHost.h"
#include "scripting/GameEvents.h"
#include "scripting/JSInterface_Entity.h"
#include "scripting/JSInterface_BaseEntity.h"
#include "scripting/JSInterface_Vector3D.h"
#include "scripting/JSInterface_Camera.h"
#include "scripting/JSInterface_Selection.h"
#include "scripting/JSInterface_Console.h"
#include "scripting/JSCollection.h"
#include "scripting/DOMEvent.h"
#ifndef NO_GUI
# include "gui/scripting/JSInterface_IGUIObject.h"
# include "gui/scripting/JSInterface_GUITypes.h"
# include "gui/GUI.h"
#endif

#include "sound/CMusicPlayer.h"
#include "sound/JSI_Sound.h"

#include "Network/SessionManager.h"
#include "Network/Server.h"
#include "Network/Client.h"




#define LOG_CATEGORY "main"

extern int conInputHandler(const SDL_Event* ev);

// Globals

bool keys[SDLK_LAST];
bool mouseButtons[5];
int g_mouse_x=50, g_mouse_y=50;

int g_xres, g_yres;
int g_bpp;
int g_freq;
bool g_active = true;

// flag to disable extended GL extensions until fix found - specifically, crashes
// using VBOs on laptop Radeon cards
static bool g_NoGLVBO=false;
// flag to switch on shadows
static bool g_Shadows=false;
// flag to switch off pbuffers
static bool g_NoPBuffer=true;
// flag to switch on fixed frame timing (RC: I'm using this for profiling purposes)
static bool g_FixedFrameTiming=false;
static bool g_VSync = false;
static float g_LodBias = 0.0f;

static bool g_Quickstart=false;

extern CLightEnv g_LightEnv;

static bool g_EntGraph = false;

static float g_Gamma = 1.0f;

extern int game_view_handler(const SDL_Event* ev);

static CMusicPlayer MusicPlayer;

CStr g_CursorName = "test";
CStr g_ActiveProfile = "default";

extern size_t frameCount;
static bool quit = false;	// break out of main loop

#ifdef ATLAS
static bool g_Atlas = true; // allows optional startup in Atlas vs non-Atlas (game) modes
#endif


const wchar_t* HardcodedErrorString(int err)
{
#define E(sym) case sym: return L ## #sym;

	switch(err)
	{
	E(ERR_NO_MEM)
	E(ERR_FILE_NOT_FOUND)
	E(ERR_INVALID_HANDLE)
	E(ERR_INVALID_PARAM)
	E(ERR_EOF)
	E(ERR_PATH_NOT_FOUND)
	E(ERR_PATH_LENGTH)
	default:
		return 0;
	}
}

const wchar_t* ErrorString(int err)
{
	// language file not available (yet)
	if(1)
		return HardcodedErrorString(err);

	// TODO: load from language file
}


ERROR_GROUP(System);
ERROR_TYPE(System, SDLInitFailed);
ERROR_TYPE(System, VmodeFailed);
ERROR_TYPE(System, RequiredExtensionsMissing);

void Testing (void)
{
	g_Console->InsertMessage(L"Testing Function Registration");
}




static std::string SplitExts(const char *exts)
{
	std::string str = exts;
	std::string ret = "";
	size_t idx = str.find_first_of(" ");
	while(idx != std::string::npos)
	{
		if(idx >= str.length() - 1)
		{
			ret += str;
			break;
		}

		ret += str.substr(0, idx);
		ret += "\n";
		str = str.substr(idx + 1);
		idx = str.find_first_of(" ");
	}

	return ret;
}


static void WriteSystemInfo()
{
TIMER(write_sys_info);

	get_gfx_info();
	// get_cpu_info already called during init - see call site
	get_snd_info();
	get_mem_status();

	struct utsname un;
	uname(&un);

	FILE* f = fopen("../logs/system_info.txt", "w");
	if(!f)
		return;

	// .. OS
	fprintf(f, "OS             : %s %s (%s)\n", un.sysname, un.release, un.version);

	// .. CPU
	fprintf(f, "CPU            : %s, %s", un.machine, cpu_type);
	if(cpus > 1)
		fprintf(f, " (x%d)", cpus);
	if(cpu_freq != 0.0f)
	{
		if(cpu_freq < 1e9)
			fprintf(f, ", %.2f MHz\n", cpu_freq*1e-6);
		else
			fprintf(f, ", %.2f GHz\n", cpu_freq*1e-9);
	}
	else
		fprintf(f, "\n");

	// .. memory
	fprintf(f, "Memory         : %lu MiB; %lu MiB free\n", tot_mem/MiB, avl_mem/MiB);

	// .. graphics
	fprintf(f, "Graphics Card  : %s\n", gfx_card);
	fprintf(f, "OpenGL Drivers : %s; %s\n", glGetString(GL_VERSION), gfx_drv_ver);
	fprintf(f, "Video Mode     : %dx%d:%d@%d\n", g_xres, g_yres, g_bpp, g_freq);

	// .. sound
	fprintf(f, "Sound Card     : %s\n", snd_card);
	fprintf(f, "Sound Drivers  : %s\n", snd_drv_ver);


	//
	// .. network name / ips
	//

	// note: can't use un.nodename because it is for an
	// "implementation-defined communications network".
	char hostname[128] = "(unknown)";
	(void)gethostname(hostname, sizeof(hostname)-1);
		// -1 makes sure it's 0-terminated. if the function fails,
		// we display "(unknown)" and will skip IP output below.
	fprintf(f, "Network Name   : %s", hostname);

	{
	hostent* host = gethostbyname(hostname);
	if(!host)
		goto no_ip;
	struct in_addr** ips = (struct in_addr**)host->h_addr_list;
	if(!ips)
		goto no_ip;

	// output all IPs (> 1 if using VMware or dual ethernet)
	fprintf(f, " (");
	for(uint i = 0; i < 256 && ips[i]; i++)	// safety
	{
		// separate entries but avoid trailing comma
		if(i != 0)
			fprintf(f, ", ");
		fprintf(f, "%s", inet_ntoa(*ips[i]));
	}
	fprintf(f, ")");
	}

no_ip:
	fprintf(f, "\n");


	// .. OpenGL extensions (write them last, since it's a lot of text)
	const char* exts = oglExtList();
	if (!exts) exts = "{unknown}";
	fprintf(f, "\nOpenGL Extensions: \n%s\n", SplitExts(exts).c_str());

	fclose(f);
	f = 0;
}


static int SetVideoMode(int w, int h, int bpp, bool fullscreen)
{
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	ulong flags = SDL_OPENGL;
	if(fullscreen)
		flags |= SDL_FULLSCREEN;
	if(!SDL_SetVideoMode(w, h, bpp, flags))
		return -1;

	glViewport(0, 0, w, h);

#ifndef NO_GUI
	g_GUI.UpdateResolution();
#endif

	oglInit();	// required after each mode change

	if(SDL_SetGamma(g_Gamma, g_Gamma, g_Gamma) < 0)
		debug_warn("SDL_SetGamma failed");

	return 0;
}


// <extension> identifies the file format that is to be written
// (case-insensitive). examples: "bmp", "png", "jpg".
// BMP is good for quick output at the expense of large files.
static void WriteScreenshot(const char* extension = "png")
{
	// determine next screenshot number.
	//
	// current approach: increment number until that file doesn't yet exist.
	// this is fairly slow, but it's typically only done once, since the last
	// number is cached. binary search shouldn't be necessary.
	//
	// known bug: after program restart, holes in the number series are
	// filled first. example: add 1st and 2nd; [exit] delete 1st; [restart]
	// add 3rd -> it gets number 1, not 3. 
	// could fix via enumerating all files, but it's not worth it ATM.
	char fn[VFS_MAX_PATH];

	const char* file_format_string = "screenshots/screenshot%04d.%s";
		// %04d -> always 4 digits, so sorting by filename works correctly.

	static int next_num = 1;
	do
		sprintf(fn, file_format_string, next_num++, extension);
	while(vfs_exists(fn));

	const int w = g_xres, h = g_yres;
	const int bpp = 24;
	GLenum fmt = GL_RGB;
	int flags = TEX_BOTTOM_UP;
	// we want writing BMP to be as fast as possible,
	// so read data from OpenGL in BMP format to obviate conversion.
	if(!stricmp(extension, "bmp"))
	{
		fmt = GL_BGR;
		flags |= TEX_BGR;
	}
		
	const size_t size = w * h * bpp;
	void* img = mem_alloc(size);

	glReadPixels(0, 0, w, h, fmt, GL_UNSIGNED_BYTE, img);

	if(tex_write(fn, w, h, bpp, flags, img) < 0)
		debug_warn("WriteScreenshot: tex_write failed");

	mem_free(img);
}



// HACK: Let code from other files (i.e. the scripting system) quit
void kill_mainloop()
{
	quit = true;
}

static int handler(const SDL_Event* ev)
{
	int c;

	switch(ev->type)
	{
	case SDL_ACTIVEEVENT:
		g_active = ev->active.gain != 0;
		break;

	case SDL_MOUSEMOTION:
		g_mouse_x = ev->motion.x;
		g_mouse_y = ev->motion.y;
		break;

	case SDL_KEYDOWN:
		c = ev->key.keysym.sym;
		keys[c] = true;
		break;

	case SDL_HOTKEYDOWN:

		switch( ev->user.code )
		{
		case HOTKEY_EXIT:
			quit = true;
			break;

		case HOTKEY_SCREENSHOT:
			WriteScreenshot();
			break;

		case HOTKEY_PLAYMUSIC:
			{
//			MusicPlayer.open("audio/music/germanic peace 3.ogg");
//			MusicPlayer.play();
			Handle hs = snd_open(
				//"audio/music/germanic peace 3.ogg"
				"audio/voice/hellenes/soldier/Attack-ZeusSaviourandVictory.ogg"
				);
				

			snd_set_pos(hs, 0,0,0, true);
			snd_play(hs);
			}
			break;

		default:
			return( EV_PASS );
		}
		return( EV_HANDLED );

	case SDL_KEYUP:
		c = ev->key.keysym.sym;
		keys[c] = false;
		break;
	case SDL_MOUSEBUTTONDOWN:
		c = ev->button.button;
		if( c < 5 )
			mouseButtons[c] = true;
		else
			debug_warn("SDL mouse button defs changed; fix mouseButton array def");
		break;
	case SDL_MOUSEBUTTONUP:
		c = ev->button.button;
		if( c < 5 )
			mouseButtons[c] = false;
		else
			debug_warn("SDL mouse button defs changed; fix mouseButton array def");
		break;
	}

	return EV_PASS;
}


void EndGame()
{
	if (g_NetServer)
	{
		delete g_NetServer;
		g_NetServer=NULL;
	}
	else if (g_NetClient)
	{
		delete g_NetClient;
		g_NetClient=NULL;
	}

	delete g_Game;
	g_Game=NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// RenderNoCull: render absolutely everything to a blank frame to force renderer
// to load required assets
void RenderNoCull()
{
	g_Renderer.BeginFrame();

	if (g_Game)
		g_Game->GetView()->RenderNoCull();

	g_Renderer.FlushFrame();
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	g_Renderer.EndFrame();
}


static void Render()
{
	MICROLOG(L"begin frame");

	oglCheck();

#ifndef NO_GUI // HACK: because colour-parsing requires the GUI
	CStr skystring = "61 193 255";
	CConfigValue* val;
	if ((val=g_ConfigDB.GetValue(CFG_USER, "skycolor")))
		val->GetString(skystring);
	CColor skycol;
	GUI<CColor>::ParseString(skystring, skycol);
	g_Renderer.SetClearColor(skycol.Int());
#endif

	// start new frame
	g_Renderer.BeginFrame();

	oglCheck();

	if (g_Game && g_Game->IsGameStarted())
	{
		g_Game->GetView()->Render();

		oglCheck();

		MICROLOG(L"flush frame");
		PROFILE_START( "flush frame" );
		g_Renderer.FlushFrame();
		PROFILE_END( "flush frame" );

		glPushAttrib( GL_ENABLE_BIT );
		glDisable( GL_LIGHTING );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_DEPTH_TEST );
		
		if( g_EntGraph )
		{
			PROFILE( "render entity overlays" );
			glColor3f( 1.0f, 0.0f, 1.0f );

			MICROLOG(L"render entities");
			g_EntityManager.renderAll(); // <-- collision outlines, pathing routes
		}

		PROFILE_START( "render selection" );
		g_Mouseover.renderSelectionOutlines();
		g_Selection.renderSelectionOutlines();
		PROFILE_END( "render selection" );

		glPopAttrib();
	}
	else
	{
		PROFILE_START( "flush frame" );
		g_Renderer.FlushFrame();
		PROFILE_END( "flush frame" );
	}

	oglCheck();

	PROFILE_START( "render fonts" );
	MICROLOG(L"render fonts");
	// overlay mode
	glPushAttrib(GL_ENABLE_BIT);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.f, (float)g_xres, 0.f, (float)g_yres, -1.f, 1000.f);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	PROFILE_END( "render fonts" );

	oglCheck();

#ifndef NO_GUI
	// Temp GUI message GeeTODO
	glLoadIdentity();
	MICROLOG(L"render GUI");
	PROFILE_START( "render gui" );
	g_GUI.Draw();
	PROFILE_END( "render gui" );
#endif

	oglCheck();

	// Text:

	// Use the GL_ALPHA texture as the alpha channel with a flat colouring
	glDisable(GL_ALPHA_TEST);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	// Added --
	glEnable(GL_TEXTURE_2D);
	// -- GL

	oglCheck();

	{
		PROFILE( "render console" );
		glLoadIdentity();

		MICROLOG(L"render console");
		CFont font("console");
		font.Bind();
		g_Console->Render();
	}

	oglCheck();


	// Profile information

	PROFILE_START( "render profiling" );
	RenderProfile();
	PROFILE_END( "render profiling" );

	oglCheck();

	if (g_Game && g_Game->IsGameStarted())
	{
		PROFILE( "render selection overlays" );
		g_Mouseover.renderOverlays();
		g_Selection.renderOverlays();
	}

	oglCheck();

	// Draw the cursor (or set the Windows cursor, on Windows)
	cursor_draw(g_CursorName);

	// restore
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glPopAttrib();

	MICROLOG(L"end frame");
	g_Renderer.EndFrame();

	oglCheck();
}


//----------------------------------------------------------------------------
// config and profile
//----------------------------------------------------------------------------

static void LoadProfile( CStr profile )
{
	CStr base = CStr( "profiles/" ) + profile;
	g_ConfigDB.SetConfigFile( CFG_USER, true, base +  "/settings/user.cfg" );
	g_ConfigDB.Reload( CFG_USER );
	int history_size = 50;
	CConfigValue* config_value = g_ConfigDB.GetValue( CFG_USER, "console.history.size" );
	if( config_value )
		config_value->GetInt( history_size );
	g_Console->UseHistoryFile( base + "/settings/history", ( history_size >= 0 ) ? history_size : 50 );
}


// Fill in the globals from the config files.
static void LoadGlobals()
{
	CConfigValue *val;

	if ((val=g_ConfigDB.GetValue(CFG_SYSTEM, "profile")))
		val->GetString( g_ActiveProfile );

	// Now load the profile before trying to retrieve the values of the rest of these.

	LoadProfile( g_ActiveProfile );

	if ((val=g_ConfigDB.GetValue(CFG_USER, "xres")))
		val->GetInt(g_xres);
	if ((val=g_ConfigDB.GetValue(CFG_USER, "yres")))
		val->GetInt(g_yres);

	if ((val=g_ConfigDB.GetValue(CFG_USER, "vsync")))
		val->GetBool(g_VSync);
	if ((val=g_ConfigDB.GetValue(CFG_USER, "novbo")))
		val->GetBool(g_NoGLVBO);
	if ((val=g_ConfigDB.GetValue(CFG_USER, "shadows")))
		val->GetBool(g_Shadows);

	if ((val=g_ConfigDB.GetValue(CFG_USER, "lodbias")))
		val->GetFloat(g_LodBias);

	if ((val=g_ConfigDB.GetValue(CFG_USER, "sound.mastergain")))
	{
		float gain;
		val->GetFloat(gain);
		int ret = snd_set_master_gain(gain);
		debug_assert(ret == 0);
	}

	LOG(NORMAL, LOG_CATEGORY, "g_x/yres is %dx%d", g_xres, g_yres);
	LOG(NORMAL, LOG_CATEGORY, "Active profile is %s", g_ActiveProfile.c_str());
}


static void ParseArgs(int argc, char* argv[])
{
	for(int i = 1; i < argc; i++)
	{
		// this arg isn't an option; skip
		if(argv[i][0] != '-')
			continue;

		char* name = argv[i]+1;	// no leading '-'

		// switch first letter of option name
		switch(argv[i][1])
		{
		case 'c':
			if(strcmp(name, "conf") == 0)
			{
				if(argc-i >= 1) // at least one arg left
				{
					i++;
					char* arg = argv[i];
					char* equ = strchr(arg, '=');
					if(equ)
					{
						*equ = 0;
						g_ConfigDB.CreateValue(CFG_COMMAND, arg)
							->m_String = (equ+1);
					}
				}
			}
			break;
		case 'e':
			g_EntGraph = true;
			break;
		case 'f':
			if(strncmp(name, "fixedframe", 10) == 0)
				g_FixedFrameTiming=true;
			break;
		case 'g':
			if(strncmp(name, "g=", 2) == 0)
			{
				g_Gamma = (float)atof(argv[i] + 3);
				if(g_Gamma == 0.0f)
					g_Gamma = 1.0f;
			}
			break;
		case 'l':
			if(strncmp(name, "listfiles", 9) == 0)
				vfs_enable_file_listing(true);
			break;
		case 'n':
			if(strncmp(name, "novbo", 5) == 0)
				g_ConfigDB.CreateValue(CFG_COMMAND, "novbo")->m_String="true";
			else if(strncmp(name, "nopbuffer", 9) == 0)
				g_NoPBuffer = true;
			break;
		case 'q':
			if(strncmp(name, "quickstart", 10) == 0)
				g_Quickstart = true;
			break;
		case 's':
			if(strncmp(name, "shadows", 7) == 0)
				g_ConfigDB.CreateValue(CFG_COMMAND, "shadows")->m_String="true";
			break;
		case 'v':
			g_ConfigDB.CreateValue(CFG_COMMAND, "vsync")->m_String="true";
			break;
		case 'x':
			if(strncmp(name, "xres=", 6) == 0)
				g_ConfigDB.CreateValue(CFG_COMMAND, "xres")->m_String=argv[i]+6;
			break;
		case 'y':
			if(strncmp(name, "yres=", 6) == 0)
				g_ConfigDB.CreateValue(CFG_COMMAND, "yres")->m_String=argv[i]+6;
			break;
		case 'p':
			if(strncmp(name, "profile=", 8) == 0 )
				g_ConfigDB.CreateValue(CFG_COMMAND, "profile")->m_String = argv[i]+9;
			break;
		}	// switch
	}
}


static void InitConfig(int argc, char* argv[])
{
	debug_printf("INITCONFIG &argc=%p &argv=%p\n", &argc, &argv);

	TIMER(InitConfig);
	MICROLOG(L"init config");

	new CConfigDB;

	g_ConfigDB.SetConfigFile(CFG_SYSTEM, false, "config/system.cfg");
	g_ConfigDB.Reload(CFG_SYSTEM);

	g_ConfigDB.SetConfigFile(CFG_MOD, true, "config/mod.cfg");
	// No point in reloading mod.cfg here - we haven't mounted mods yet

	ParseArgs(argc, argv);

	LoadGlobals(); // Collects information from system.cfg, the profile file, and any command-line overrides
	// to fill in the globals.
}

//----------------------------------------------------------------------------


static void InitScripting()
{
TIMER(InitScripting);
	// Create the scripting host.  This needs to be done before the GUI is created.
	new ScriptingHost;

	// It would be nice for onLoad code to be able to access the setTimeout() calls.
	new CScheduler;

	// Register the JavaScript interfaces with the runtime
	CEntity::ScriptingInit();
	CBaseEntity::ScriptingInit();
	JSI_Sound::ScriptingInit();
	CProfileNode::ScriptingInit();

#ifndef NO_GUI
	JSI_IGUIObject::init();
	JSI_GUITypes::init();
#endif
	JSI_Vector3D::init();
	EntityCollection::Init( "EntityCollection" );
	SColour::ScriptingInit();
	CPlayer::ScriptingInit();
	
	PlayerCollection::Init( "PlayerCollection" );
	CDamageType::ScriptingInit();
	CJSComplexPropertyAccessor<CEntity>::ScriptingInit(); // <-- Doesn't really matter which we use, but we know CJSPropertyAccessor<T> is already being compiled for T = CEntity.
	CScriptEvent::ScriptingInit();
	CJSProgressTimer::ScriptingInit();
	CProjectile::ScriptingInit();

	g_ScriptingHost.DefineConstant( "ORDER_NONE", -1 );
	g_ScriptingHost.DefineConstant( "ORDER_GOTO", CEntityOrder::ORDER_GOTO );
	g_ScriptingHost.DefineConstant( "ORDER_PATROL", CEntityOrder::ORDER_PATROL );
	g_ScriptingHost.DefineConstant( "ORDER_ATTACK", CEntityOrder::ORDER_ATTACK_MELEE );
	g_ScriptingHost.DefineConstant( "ORDER_GATHER", CEntityOrder::ORDER_GATHER );
	
#define REG_JS_CONSTANT(_name) g_ScriptingHost.DefineConstant(#_name, _name)
	REG_JS_CONSTANT(SDL_BUTTON_LEFT);
	REG_JS_CONSTANT(SDL_BUTTON_MIDDLE);
	REG_JS_CONSTANT(SDL_BUTTON_RIGHT);
	REG_JS_CONSTANT(SDL_BUTTON_WHEELUP);
	REG_JS_CONSTANT(SDL_BUTTON_WHEELDOWN);
#undef REG_JS_CONSTANT
	
	CNetMessage::ScriptingInit();

	JSI_Camera::init();
	JSI_Console::init();

	new CGameEvents;
}


static void InitVfs(const char* argv0)
{
TIMER(InitVfs);
	// set current directory to "$game_dir/data".
	// this is necessary because it is otherwise unknown,
	// especially if run from a shortcut / symlink.
	//
	// "../data" is relative to the executable (in "$game_dir/system").
	//
	// rationale for data/ being root: untrusted scripts must not be
	// allowed to overwrite critical game (or worse, OS) files.
	// the VFS prevents any accesses to files above this directory.
	int err = file_rel_chdir(argv0, "../data");
	if(err < 0)
		throw err;

	{
	TIMER(VFS_INIT);
	vfs_init();
	vfs_mount("", "mods/official", VFS_MOUNT_RECURSIVE|VFS_MOUNT_ARCHIVES|VFS_MOUNT_WATCH);
	vfs_mount("screenshots/", "screenshots");
	vfs_mount("profiles/", "profiles", VFS_MOUNT_RECURSIVE);
	}
	// don't try vfs_display yet: SDL_Init hasn't yet redirected stdout
}


static void InitPs()
{
	// console
	{
	TIMER(ps_console);

		g_Console->UpdateScreenSize(g_xres, g_yres);

		// Calculate and store the line spacing
		CFont font("console");
		g_Console->m_iFontHeight = font.GetLineSpacing();
		// Offset by an arbitrary amount, to make it fit more nicely
		g_Console->m_iFontOffset = 9;
	}

	// language and hotkeys
	{
	TIMER(ps_lang_hotkeys);

		CConfigValue* val = g_ConfigDB.GetValue(CFG_SYSTEM, "language");
		std::string lang = "english";
		if (val)
			val->GetString(lang);
		I18n::LoadLanguage(lang.c_str());

		loadHotkeys();
	}

#ifndef NO_GUI
	{
		// GUI uses VFS, so this must come after VFS init.
		{TIMER(ps_gui_init);
		g_GUI.Initialize();}

		{TIMER(ps_gui_setup_xml);
		g_GUI.LoadXMLFile("gui/test/setup.xml");}
		{TIMER(ps_gui_styles_xml);
		g_GUI.LoadXMLFile("gui/test/styles.xml");}
		{TIMER(ps_gui_sprite1_xml);
		g_GUI.LoadXMLFile("gui/test/sprite1.xml");}
	}

	// Temporary hack until revised GUI structure is completed.
#ifdef ATLAS
	if (! g_Atlas)
#endif
	{
//	TIMER(ps_gui_hack)

		{TIMER(ps_gui_1);
		g_GUI.LoadXMLFile("gui/test/1_init.xml");}
		{TIMER(ps_gui_2);
		g_GUI.LoadXMLFile("gui/test/2_mainmenu.xml");}
		{TIMER(ps_gui_3);
		g_GUI.LoadXMLFile("gui/test/3_loading.xml");}
		{TIMER(ps_gui_4);
		g_GUI.LoadXMLFile("gui/test/4_session.xml");}
		{TIMER(ps_gui_6);
		g_GUI.LoadXMLFile("gui/test/6_subwindows.xml");}
		{TIMER(ps_gui_6_1);
		g_GUI.LoadXMLFile("gui/test/6_1_manual.xml");}
		{TIMER(ps_gui_6_2);
		g_GUI.LoadXMLFile("gui/test/6_2_jukebox.xml");}
		{TIMER(ps_gui_7);
		g_GUI.LoadXMLFile("gui/test/7_atlas.xml");}
		{TIMER(ps_gui_9);
		g_GUI.LoadXMLFile("gui/test/9_global.xml");}
	}

//	{
//	TIMER(ps_gui_hello_xml);
//		g_GUI.LoadXMLFile("gui/test/hello.xml");
//	}
#endif
}

static void psShutdown()
{
#ifndef NO_GUI
	g_GUI.Destroy();
	delete &g_GUI;
#endif

	delete g_Console;

	// disable the special Windows cursor, or free textures for OGL cursors
	cursor_draw(NULL);

	// close down Xerces if it was loaded
	CXeromyces::Terminate();

	MusicPlayer.release();

	// Unload the real language (since it depends on the scripting engine,
	// which is going to be killed later) and use the English fallback messages
	I18n::LoadLanguage(NULL);
}


static void InitRenderer()
{
TIMER(InitRenderer);
	// create renderer
	new CRenderer;

	// set renderer options from command line options - NOVBO must be set before opening the renderer
	g_Renderer.SetOptionBool(CRenderer::OPT_NOVBO,g_NoGLVBO);
	g_Renderer.SetOptionBool(CRenderer::OPT_SHADOWS,g_Shadows);
	g_Renderer.SetOptionBool(CRenderer::OPT_NOPBUFFER,g_NoPBuffer);
	g_Renderer.SetOptionFloat(CRenderer::OPT_LODBIAS, g_LodBias);

	// create terrain related stuff
	new CTextureManager;

	// create the material manager
	new CMaterialManager;
	new CMeshManager;

	// create actor related stuff
	new CSkeletonAnimManager;
	new CObjectManager;
	new CUnitManager;

	MICROLOG(L"init renderer");
	g_Renderer.Open(g_xres,g_yres,g_bpp);

	// Setup default lighting environment. Since the Renderer accesses the
	// lighting environment through a pointer, this has to be done before
	// the first Frame.
	g_LightEnv.m_SunColor=RGBColor(1,1,1);
	g_LightEnv.SetRotation(DEGTORAD(270));
	g_LightEnv.SetElevation(DEGTORAD(45));
	g_LightEnv.m_TerrainAmbientColor=RGBColor(0,0,0);
	g_LightEnv.m_UnitsAmbientColor=RGBColor(0.4f,0.4f,0.4f);
	g_Renderer.SetLightEnv(&g_LightEnv);

	// I haven't seen the camera affecting GUI rendering and such, but the
	// viewport has to be updated according to the video mode
	SViewPort vp;
	vp.m_X=0;
	vp.m_Y=0;
	vp.m_Width=g_xres;
	vp.m_Height=g_yres;
	g_Renderer.SetViewport(vp);
}

static void InitSDL()
{
	MICROLOG(L"init sdl");
	if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_NOPARACHUTE) < 0)
	{
		LOG(ERROR, LOG_CATEGORY, "SDL library initialization failed: %s", SDL_GetError());
		throw PSERROR_System_SDLInitFailed();
	}
	atexit(SDL_Quit);
	SDL_EnableUNICODE(1);
}


static int ProgressiveLoad()
{
	wchar_t description[100];
	int progress_percent;
	int ret = LDR_ProgressiveLoad(10e-3, description, ARRAY_SIZE(description), &progress_percent);
	switch(ret)
	{
	// no load active => no-op (skip code below)
	case 0:
		return 0;
	// current task didn't complete. we only care about this insofar as the
	// load process is therefore not yet finished.
	case ERR_TIMED_OUT:
		break;
	// just finished loading
	case LDR_ALL_FINISHED:
		g_Game->ReallyStartGame();
		wcscpy_s(description, ARRAY_SIZE(description), L"Game is starting..");
			// LDR_ProgressiveLoad returns L""; set to valid text to
			// avoid problems in converting to JSString
		break;
	// error!
	default:
		CHECK_ERR(ret);
			// can't do this above due to legit ERR_TIMED_OUT
		break;
	}

#ifndef NO_GUI
	// display progress / description in loading screen
	CStrW i18n_description = I18n::translate(description);
	JSString* js_desc = StringConvert::wstring_to_jsstring(g_ScriptingHost.getContext(), i18n_description);
	g_ScriptingHost.SetGlobal("g_Progress", INT_TO_JSVAL(progress_percent)); 
	g_ScriptingHost.SetGlobal("g_LoadDescription", STRING_TO_JSVAL(js_desc));
	g_GUI.SendEventToAll("progress");
#endif

	return 0;
}



u64 PREVTSC;

static void Shutdown()
{
	psShutdown(); // Must delete g_GUI before g_ScriptingHost

	if (g_Game)
		EndGame();

	delete &g_Scheduler;

	delete &g_SessionManager;

	delete &g_Mouseover;
	delete &g_Selection;

	delete &g_Pathfinder;
	// Managed by CWorld
	// delete &g_EntityManager;

	delete &g_GameAttributes;
	delete &g_JSGameEvents;

	delete &g_EntityTemplateCollection;

	delete &g_ScriptingHost;

	// destroy actor related stuff
	delete &g_UnitMan;
	delete &g_ObjMan;
	delete &g_SkelAnimMan;

	delete &g_MaterialManager;
	delete &g_MeshManager;

	// destroy terrain related stuff
	delete &g_TexMan;

	// destroy renderer
	delete &g_Renderer;

	delete &g_ConfigDB;

	// Shut down the network loop
	CSocketBase::Shutdown();

	// Really shut down the i18n system. Any future calls
	// to translate() will crash.
	I18n::Shutdown();

	snd_shutdown();

	vfs_shutdown();

	h_mgr_shutdown();
	mem_shutdown();

	delete &g_Logger;

	delete &g_Profiler;
}


// workaround for VC7 EBP-trashing bug, which confuses the stack trace code.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif

static void Init(int argc, char* argv[], bool setup_gfx = true)
{
debug_printf("INIT &argc=%p &argv=%p\n", &argc, &argv);

	MICROLOG(L"In init");

	// If you ever want to catch a particular allocation:
	//_CrtSetBreakAlloc(187);

	new CLogger;

	// no longer set 24 bit (float) precision by default: for
	// very long game uptimes (> 1 day; e.g. dedicated server),
	// we need full precision when calculating the time.
	// if there's a spot where we want to speed up divides|sqrts,
	// we can temporarily change precision there.
//	_control87(_PC_24, _MCW_PC);

	// detects CPU clock frequency and capabilities, which are prerequisites
	// for using the TSC as a timer (desirable due to its high resolution).
	// do this before lengthy init so we can time those accurately.
	get_cpu_info();

	// Call LoadLanguage(NULL) to initialise the I18n system, but
	// without loading an actual language file - translate() will
	// just show the English key text, which is better than crashing
	// from a null pointer when attempting to translate e.g. error messages.
	// Real languages can only be loaded when the scripting system has
	// been initialised.
	MICROLOG(L"init i18n");
	I18n::LoadLanguage(NULL);

	// Do this as soon as possible, because it chdirs
	// and will mess up the error reporting if anything
	// crashes before the working directory is set.
	MICROLOG(L"init vfs");
	const char* argv0 = argc? argv[0] : NULL;
		// ScEd doesn't have a main(argc, argv), and so it has no argv. In that
		// case, just pass NULL to InitVfs, which will work out the current
		// directory for itself.
	InitVfs(argv0);

	// Set up the console early, so that debugging
	// messages can be logged to it. (The console's size
	// and fonts are set later in InitPs())
	g_Console = new CConsole();

	if(setup_gfx)
		InitSDL();

	// preferred video mode = current desktop settings
	// (command line params may override these)
	get_cur_vmode(&g_xres, &g_yres, &g_bpp, &g_freq);

	new CProfileManager;	// before any script code

	MICROLOG(L"init scripting");
	InitScripting();	// before GUI
	
	// g_ConfigDB, command line args, globals
	InitConfig(argc, argv);

	// GUI is notified in SetVideoMode, so this must come before that.
#ifndef NO_GUI
	new CGUI;
#endif

	CConfigValue *val=g_ConfigDB.GetValue(CFG_SYSTEM, "windowed");
	bool windowed=false;
	if (val) val->GetBool(windowed);

	if (setup_gfx)
	{
		MICROLOG(L"set vmode");

		if(SetVideoMode(g_xres, g_yres, 32, !windowed) < 0)
		{
			LOG(ERROR, LOG_CATEGORY, "Could not set %dx%d graphics mode: %s", g_xres, g_yres, SDL_GetError());
			throw PSERROR_System_VmodeFailed();
		}
		SDL_WM_SetCaption("0 A.D.", "0 A.D.");
	}

	if(!g_Quickstart)
	{
		WriteSystemInfo();
		vfs_display();
	}
	else
	{
		// speed up startup by disabling all sound
		// (OpenAL init will be skipped).
		// must be called before first snd_open.
		snd_disable(true);
	}

	// (must come after SetVideoMode, since it calls oglInit)
	const char* missing = oglHaveExtensions(0, "GL_ARB_multitexture", "GL_ARB_texture_env_combine", "GL_ARB_texture_env_dot3", 0);
	if(missing)
	{
		wchar_t buf[500];
		const wchar_t* fmt =
			L"The %hs extension doesn't appear to be available on your computer. "
			L"The game may still work, though - you are welcome to try at your own risk. "
			L"If not or it doesn't look right, upgrade your graphics card.";
		swprintf(buf, ARRAY_SIZE(buf), fmt, missing);
		DISPLAY_ERROR(buf);
		// TODO: i18n
	}

	// enable/disable VSync
	// note: "GL_EXT_SWAP_CONTROL" is "historical" according to dox.
#ifdef OS_WIN
	if(oglHaveExtension("WGL_EXT_swap_control"))
		wglSwapIntervalEXT(g_VSync? 1 : 0);
#endif

	MICROLOG(L"init ps");
	InitPs();

	InitRenderer();

TIMER(init_after_InitRenderer);

	// This needs to be done after the renderer has loaded all its actors...
	new CBaseEntityCollection;
	// CEntityManager is managed by CWorld
	//new CEntityManager;
	new CPathfindEngine;
	new CSelectedEntities;
	new CMouseoverEntities;

	new CSessionManager;

	new CGameAttributes;

	// Register a few Game/Network JS globals
	g_ScriptingHost.SetGlobal("g_GameAttributes", OBJECT_TO_JSVAL(g_GameAttributes.GetScript()));

	// Check for heap corruption after every allocation. Very, very slowly.
	// (And it highlights the allocation just after the one you care about,
	// so you need to run it again and tell it to break on the one before.)
//	debug_heap_enable(DEBUG_HEAP_ALL);

	// register input handlers
	{
		// This stack is constructed so the first added, will be the last
		//  one called. This is important, because each of the handlers
		//  has the potential to block events to go further down
		//  in the chain. I.e. the last one in the list added, is the
		//  only handler that can block all messages before they are
		//  processed.
		in_add_handler(game_view_handler);

		in_add_handler(interactInputHandler);

		in_add_handler(conInputHandler);

		in_add_handler(profilehandler);

		in_add_handler(hotkeyInputHandler); 

		// I don't know how much this screws up, but the gui_handler needs
		//  to be after the hotkey, so that input boxes can be typed in
		//  without setting off hotkeys.
#ifndef NO_GUI
		in_add_handler(gui_handler);
#endif

		in_add_handler(handler); // must be after gui_handler. Should mayhap even be last.
	}

#ifndef NO_GUI
	g_GUI.SendEventToAll("load");
#endif

	if (setup_gfx)
	{
		MICROLOG(L"render blank");
		// render everything to a blank frame to force renderer to load everything
		RenderNoCull();
	}

	if (g_FixedFrameTiming) {
		CCamera &g_Camera=*g_Game->GetView()->GetCamera();
#if 0		// TOPDOWN
		g_Camera.SetProjection(1.0f,10000.0f,DEGTORAD(90));
		g_Camera.m_Orientation.SetIdentity();
		g_Camera.m_Orientation.RotateX(DEGTORAD(90));
		g_Camera.m_Orientation.Translate(CELL_SIZE*250*0.5, 250, CELL_SIZE*250*0.5);
#else		// std view
		g_Camera.SetProjection(1.0f,10000.0f,DEGTORAD(20));
		g_Camera.m_Orientation.SetXRotation(DEGTORAD(30));
		g_Camera.m_Orientation.RotateY(DEGTORAD(-45));
		g_Camera.m_Orientation.Translate(350, 350, -275);
#endif
		g_Camera.UpdateFrustum();
	}

	g_Console->RegisterFunc(Testing, L"Testing");
}

#ifdef _MSC_VER
#pragma optimize("", on)	// restore; see above.
#endif



static void Frame()
{
	MICROLOG(L"In frame");


	PROFILE_START( "update music" );
	MusicPlayer.update();
	PROFILE_END( "update music" );

	static double last_time;
	const double time = get_time();
	const float TimeSinceLastFrame = (float)(time-last_time);
	last_time = time;
	ONCE(return);
			// first call: set last_time and return
	debug_assert(TimeSinceLastFrame >= 0.0f);

	PROFILE_START( "reload changed files" );
	MICROLOG(L"reload files");
	res_reload_changed_files(); 
	PROFILE_END( "reload changed files" );

	PROFILE_START( "progressive load" );
	ProgressiveLoad();
	PROFILE_END( "progressive load" );

	PROFILE_START( "input" );
	MICROLOG(L"input");
	in_get_events();
	g_SessionManager.Poll();
	PROFILE_END( "input" );

	PROFILE_START( "gui tick" );
#ifndef NO_GUI
	g_GUI.TickObjects();
#endif
	PROFILE_END( "gui tick" );
	
	PROFILE_START( "game logic" );
	if (g_Game && g_Game->IsGameStarted())
	{
		PROFILE_START( "simulation update" );
		g_Game->Update(TimeSinceLastFrame);
		PROFILE_END( "simulation update" );

		if (!g_FixedFrameTiming)
		{
			PROFILE( "camera update" );
			g_Game->GetView()->Update(float(TimeSinceLastFrame));
		}

		PROFILE_START( "selection and interaction ui" );
		// TODO Where does GameView end and other things begin?
		g_Mouseover.update( TimeSinceLastFrame );
		g_Selection.update();
		PROFILE_END( "selection and interaction ui" );

		PROFILE_START( "sound update" );
		CCamera* camera = g_Game->GetView()->GetCamera();
		CMatrix3D& orientation = camera->m_Orientation;

		float* pos = &orientation._data[12];
		float* dir = &orientation._data[8];
		float* up  = &orientation._data[4];
		if(snd_update(pos, dir, up) < 0)
			debug_printf("snd_update failed\n");
		PROFILE_END( "sound update" );
	}
	else
	{
		// CSimulation would do this with the proper turn length if we were in
		// a game. This is basically just to keep script timers running.
		uint ms_elapsed = (uint)(TimeSinceLastFrame*1000);
		g_Scheduler.update(ms_elapsed);
		if(snd_update(0, 0, 0) < 0)
			debug_printf("snd_update (pos=0 version) failed\n");
	}

	PROFILE_END( "game logic" );

	PROFILE_START( "update console" );
	g_Console->Update(TimeSinceLastFrame);
	PROFILE_END( "update console" );

	// ugly, but necessary. these are one-shot events, have to be reset.

	// Spoof mousebuttonup events for the hotkey system
	SDL_Event spoof;
	spoof.type = SDL_MOUSEBUTTONUP;
	spoof.button.button = SDL_BUTTON_WHEELUP;
	if( mouseButtons[SDL_BUTTON_WHEELUP] )
		hotkeyInputHandler( &spoof );
	spoof.button.button = SDL_BUTTON_WHEELDOWN;
	if( mouseButtons[SDL_BUTTON_WHEELDOWN] )
		hotkeyInputHandler( &spoof );

	mouseButtons[SDL_BUTTON_WHEELUP] = false;
	mouseButtons[SDL_BUTTON_WHEELDOWN] = false;

	PROFILE_START( "render" );
	if(g_active)
	{
		MICROLOG(L"render");
		Render();
		MICROLOG(L"finished render");
		PROFILE_START( "swap buffers" );
		SDL_GL_SwapBuffers();
		PROFILE_END( "swap buffers" );
	}
	// inactive; relinquish CPU for a little while
	// don't use SDL_WaitEvent: don't want the main loop to freeze until app focus is restored
	else
		SDL_Delay(10);

	PROFILE_END( "render" );

	g_Profiler.Frame();

	calc_fps();
	if (g_FixedFrameTiming && frameCount==100) quit=true;
}



#ifndef SCED

int main(int argc, char* argv[])
{
	debug_printf("MAIN &argc=%p &argv=%p\n", &argc, &argv);

#ifdef ATLAS
	if (g_Atlas)
	{
		extern void BeginAtlas(int, char**);
		BeginAtlas(argc, argv);
	}
	else
#endif
	{
		MICROLOG(L"Init");
		Init(argc, argv, true);

		// Optionally, do some simple tests to ensure things aren't broken
		//	extern void PerformTests();
		//	PerformTests();

		while(!quit)
		{
			MICROLOG(L"(Simulation) Frame");
			Frame();
		}

		MICROLOG(L"Shutdown");
		Shutdown();
	}

	exit(0);
}

// Public functions for Atlas to use:
// TODO: Make this far less hacky
void Init_(int argc, char** argv, bool setup_gfx)
{
	g_Quickstart = true;
	Init(argc, argv, setup_gfx);
}
void Shutdown_()
{
	Shutdown();
}
void Render_()
{
	Render();
}


#else // SCED:

void ScEd_Init()
{
	g_Quickstart = true;

	Init(0, NULL, false);
}

void ScEd_Shutdown()
{
	Shutdown();

	delete &g_Profiler;
}

#endif // SCED
