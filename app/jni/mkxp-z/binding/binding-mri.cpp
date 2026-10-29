/*
** binding-mri.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "audio/audio.h"
#include "filesystem/filesystem.h"
#include "display/graphics.h"
#include "display/font.h"
#include "system/system.h"

#include "util/util.h"
#include "util/sdl-util.h"
#include "util/debugwriter.h"
#include "util/boost-hash.h"
#include "util/exception.h"
#include "util/encoding.h"

#include "config.h"

#include "binding-util.h"
#include "binding.h"

#include "sharedstate.h"
#include "eventthread.h"

#include <vector>

extern "C" {
#include <ruby.h>

#if RAPI_FULL >= 190
#include <ruby/encoding.h>
#endif
}

#ifdef __WIN32__
#include "binding-mri-win32.h"
#endif

#include <assert.h>
#include <string>
#include <zlib.h>

#include <SDL_cpuinfo.h>
#include <SDL_filesystem.h>
#include <SDL_loadso.h>
#include <SDL_power.h>

extern const char module_rpg1[];
extern const char module_rpg2[];
extern const char module_rpg3[];

static void mriBindingExecute();
static void mriBindingTerminate();
static void mriBindingReset();

ScriptBinding scriptBindingImpl = {
	mriBindingExecute,
	mriBindingTerminate,
	mriBindingReset
};

ScriptBinding *scriptBinding = &scriptBindingImpl;

void tableBindingInit();
void etcBindingInit();
void fontBindingInit();
void bitmapBindingInit();
void spriteBindingInit();
void viewportBindingInit();
void planeBindingInit();
void windowBindingInit();
void tilemapBindingInit();
void windowVXBindingInit();
void tilemapVXBindingInit();

void inputBindingInit();
void audioBindingInit();
void graphicsBindingInit();

void fileIntBindingInit();

#ifdef MKXPZ_BUILD_ANDROID
void androidBindingInit();
#endif

#ifdef MKXPZ_MINIFFI
void MiniFFIBindingInit();
#endif

#ifdef MKXPZ_STEAM
void CUSLBindingInit();
#endif

void httpBindingInit();

RB_METHOD(mkxpDelta);
RB_METHOD(mriPrint);
RB_METHOD(mriP);
RB_METHOD(mkxpDataDirectory);
RB_METHOD(mkxpSetTitle);
RB_METHOD(mkxpGetTitle);
RB_METHOD(mkxpDesensitize);
RB_METHOD(mkxpPuts);

RB_METHOD(mkxpPlatform);
RB_METHOD(mkxpIsWindowsHost);
RB_METHOD(mkxpIsLinuxHost);
RB_METHOD(mkxpIsMacHost);
RB_METHOD(mkxpIsAndroidHost);
RB_METHOD(mkxpIsUsingWine);
RB_METHOD(mkxpIsUsingRosetta);
RB_METHOD(mkxpIsReallyWindowsHost);
RB_METHOD(mkxpIsReallyLinuxHost);
RB_METHOD(mkxpIsReallyMacHost);

RB_METHOD(mkxpUserLanguage);
RB_METHOD(mkxpUserName);
RB_METHOD(mkxpGameTitle);
RB_METHOD(mkxpPowerState);
RB_METHOD(mkxpSettingsMenu);
RB_METHOD(mkxpCpuCount);
RB_METHOD(mkxpSystemMemory);
RB_METHOD(mkxpReloadPathCache);
RB_METHOD(mkxpAddPath);
RB_METHOD(mkxpRemovePath);
RB_METHOD(mkxpLaunch);

RB_METHOD(mkxpGetJSONSetting);
RB_METHOD(mkxpSetJSONSetting);
RB_METHOD(mkxpGetAllJSONSettings);

RB_METHOD(mkxpSetDefaultFontFamily);

RB_METHOD(mriRgssMain);
RB_METHOD(mriRgssStop);
RB_METHOD(_kernelCaller);

RB_METHOD(mkxpStringToUTF8);
RB_METHOD(mkxpStringToUTF8Bang);

VALUE json2rb(json5pp::value const &v);
json5pp::value rb2json(VALUE v);

static void mriBindingInit()
{
	tableBindingInit();
	etcBindingInit();
	fontBindingInit();
	bitmapBindingInit();
	spriteBindingInit();
	viewportBindingInit();
	planeBindingInit();

	if (rgssVer == 1) {
		windowBindingInit();
		tilemapBindingInit();
	} else {
		windowVXBindingInit();
		tilemapVXBindingInit();
	}

	inputBindingInit();
	audioBindingInit();
	graphicsBindingInit();

	fileIntBindingInit();

#ifdef MKXPZ_BUILD_ANDROID
	androidBindingInit();
#endif

#ifdef MKXPZ_MINIFFI
	MiniFFIBindingInit();
#endif

#ifdef MKXPZ_STEAM
	CUSLBindingInit();
#endif

	httpBindingInit();

	if (rgssVer >= 3) {
		_rb_define_module_function(rb_mKernel, "rgss_main", mriRgssMain);
		_rb_define_module_function(rb_mKernel, "rgss_stop", mriRgssStop);

		_rb_define_module_function(rb_mKernel, "msgbox", mriPrint);
		_rb_define_module_function(rb_mKernel, "msgbox_p", mriP);

		rb_define_global_const("RGSS_VERSION", rb_utf8_str_new_cstr("3.0.1"));
	} else {
		_rb_define_module_function(rb_mKernel, "print", mriPrint);
		_rb_define_module_function(rb_mKernel, "p", mriP);

		rb_define_alias(rb_singleton_class(rb_mKernel), "_mkxp_kernel_caller_alias", "caller");
		_rb_define_module_function(rb_mKernel, "caller", _kernelCaller);
	}

	if (rgssVer == 1)
		rb_eval_string(module_rpg1);
	else if (rgssVer == 2)
		rb_eval_string(module_rpg2);
	else if (rgssVer == 3)
		rb_eval_string(module_rpg3);
	else
		assert(!"unreachable");

	VALUE mod = rb_define_module("System");

	_rb_define_module_function(mod, "delta", mkxpDelta);
	_rb_define_module_function(mod, "uptime", mkxpDelta);
	_rb_define_module_function(mod, "data_directory", mkxpDataDirectory);
	_rb_define_module_function(mod, "set_window_title", mkxpSetTitle);
	_rb_define_module_function(mod, "window_title", mkxpGetTitle);
	_rb_define_module_function(mod, "window_title=", mkxpSetTitle);
	_rb_define_module_function(mod, "show_settings", mkxpSettingsMenu);
	_rb_define_module_function(mod, "puts", mkxpPuts);
	_rb_define_module_function(mod, "desensitize", mkxpDesensitize);

	_rb_define_module_function(mod, "platform", mkxpPlatform);
	_rb_define_module_function(mod, "is_windows?", mkxpIsWindowsHost);
	_rb_define_module_function(mod, "is_linux?", mkxpIsLinuxHost);
	_rb_define_module_function(mod, "is_mac?", mkxpIsMacHost);
	_rb_define_module_function(mod, "is_android?", mkxpIsAndroidHost);
	_rb_define_module_function(mod, "is_wine?", mkxpIsUsingWine);
	_rb_define_module_function(mod, "is_rosetta?", mkxpIsUsingRosetta);
	_rb_define_module_function(mod, "is_really_windows?", mkxpIsReallyWindowsHost);
	_rb_define_module_function(mod, "is_really_linux?", mkxpIsReallyLinuxHost);
	_rb_define_module_function(mod, "is_really_mac?", mkxpIsReallyMacHost);

	_rb_define_module_function(mod, "user_language", mkxpUserLanguage);
	_rb_define_module_function(mod, "user_name", mkxpUserName);
	_rb_define_module_function(mod, "game_title", mkxpGameTitle);
	_rb_define_module_function(mod, "power_state", mkxpPowerState);
	_rb_define_module_function(mod, "nproc", mkxpCpuCount);
	_rb_define_module_function(mod, "memory", mkxpSystemMemory);

	_rb_define_module_function(mod, "reload_cache", mkxpReloadPathCache);
	_rb_define_module_function(mod, "mount", mkxpAddPath);
	_rb_define_module_function(mod, "unmount", mkxpRemovePath);

	_rb_define_module_function(mod, "launch", mkxpLaunch);

	_rb_define_module_function(mod, "default_font_family=", mkxpSetDefaultFontFamily);

	_rb_define_method(rb_cString, "to_utf8", mkxpStringToUTF8);
	_rb_define_method(rb_cString, "to_utf8!", mkxpStringToUTF8Bang);

	VALUE cmod = rb_define_module("CFG");
	_rb_define_module_function(cmod, "[]", mkxpGetJSONSetting);
	_rb_define_module_function(cmod, "[]=", mkxpSetJSONSetting);
	_rb_define_module_function(cmod, "to_hash", mkxpGetAllJSONSettings);

	// Load global constants
	rb_gv_set("MKXP", Qtrue);

	VALUE debug = rb_bool_new(shState->config().editor.debug);
	if (rgssVer == 1)
		rb_gv_set("DEBUG", debug);
	else if (rgssVer >= 2)
		rb_gv_set("TEST", debug);

	rb_gv_set("BTEST", rb_bool_new(shState->config().editor.battleTest));

	VALUE vers = rb_utf8_str_new_cstr(MKXPZ_VERSION);
	rb_str_freeze(vers);
	rb_define_const(mod, "VERSION", vers);

	// Set $stdout and its ilk accordingly on Windows
	// I regret teaching you that word
#ifdef __WIN32__
	if (shState->config().winConsole)
		configureWindowsStreams();
#endif
}

static void showMsg(const std::string &msg)
{
	shState->eThread().showMessageBox(msg.c_str());
}

static void printP(int argc, VALUE *argv, const char *convMethod, const char *sep)
{
	VALUE dispString = rb_str_buf_new(128);
	ID conv = rb_intern(convMethod);

	for (int i = 0; i < argc; ++i) {
		VALUE str = rb_funcall2(argv[i], conv, 0, NULL);
		rb_str_buf_append(dispString, str);

		if (i < argc)
			rb_str_buf_cat2(dispString, sep);
	}

	showMsg(RSTRING_PTR(dispString));
}

RB_METHOD(mriPrint) {
    RB_UNUSED_PARAM;
    
    printP(argc, argv, "to_s", "");
    
    return Qnil;
}

RB_METHOD(mriP) {
    RB_UNUSED_PARAM;
    
    printP(argc, argv, "inspect", "\n");
    
    return Qnil;
}

RB_METHOD(mkxpDelta) {
    RB_UNUSED_PARAM;
    
    return ULL2NUM(shState->runTime());
}

RB_METHOD(mkxpDataDirectory) {
    RB_UNUSED_PARAM;
    
    const std::string &path = shState->config().customDataPath;
    const char *s = path.empty() ? "." : path.c_str();
    
    std::string s_nml = shState->fileSystem().normalize(s, 1, 1);
    VALUE ret = rb_utf8_str_new_cstr(s_nml.c_str());
    
    return ret;
}

RB_METHOD(mkxpSetTitle) {
    RB_UNUSED_PARAM;
    
    VALUE s;
    rb_scan_args(argc, argv, "1", &s);
    SafeStringValue(s);
    
    shState->eThread().requestWindowRename(RSTRING_PTR(s));
    return s;
}

RB_METHOD(mkxpGetTitle) {
    RB_UNUSED_PARAM;
    
    rb_check_argc(argc, 0);
    
    return rb_utf8_str_new_cstr(SDL_GetWindowTitle(shState->sdlWindow()));
}

RB_METHOD(mkxpDesensitize) {
    RB_UNUSED_PARAM;
    
    VALUE filename;
    rb_scan_args(argc, argv, "1", &filename);
    SafeStringValue(filename);
    
    return rb_utf8_str_new_cstr(
                                shState->fileSystem().desensitize(RSTRING_PTR(filename)));
}

RB_METHOD(mkxpPuts) {
    RB_UNUSED_PARAM;
    
    const char *str;
    rb_get_args(argc, argv, "z", &str RB_ARG_END);
    
    Debug() << str;
    
    return Qnil;
}

RB_METHOD(mkxpPlatform)
{
	RB_UNUSED_PARAM;

#if MKXPZ_PLATFORM == MKXPZ_PLATFORM_MACOS
	std::string platform("macOS");

	if (mkxp_sys::isRosetta())
		platform += " (Rosetta)";
#elif MKXPZ_PLATFORM == MKXPZ_PLATFORM_WINDOWS
	std::string platform("Windows");

	if (mkxp_sys::isWine()) {
		platform += " (Wine - ";
		switch (mkxp_sys::getRealHostType())
		{
			case mkxp_sys::WineHostType::Mac:
				platform += "macOS)";
				break;
			default:
				platform += "Linux)";
				break;
		}
	}
#elif MKXPZ_PLATFORM == MKXPZ_PLATFORM_ANDROID
	std::string platform("Android");
#else
	std::string platform("Linux");
#endif

	return rb_utf8_str_new_cstr(platform.c_str());
}

RB_METHOD(mkxpIsWindowsHost)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(MKXPZ_PLATFORM == MKXPZ_PLATFORM_WINDOWS);
}

RB_METHOD(mkxpIsLinuxHost)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(MKXPZ_PLATFORM == MKXPZ_PLATFORM_LINUX || MKXPZ_PLATFORM == MKXPZ_PLATFORM_ANDROID);
}

RB_METHOD(mkxpIsMacHost)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(MKXPZ_PLATFORM == MKXPZ_PLATFORM_MACOS);
}

RB_METHOD(mkxpIsAndroidHost)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(MKXPZ_PLATFORM == MKXPZ_PLATFORM_ANDROID);
}

RB_METHOD(mkxpIsUsingWine)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(mkxp_sys::isWine());
}

RB_METHOD(mkxpIsUsingRosetta)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(mkxp_sys::isRosetta());
}

RB_METHOD(mkxpIsReallyWindowsHost)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(mkxp_sys::getRealHostType() == mkxp_sys::WineHostType::Windows);
}

RB_METHOD(mkxpIsReallyLinuxHost)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(mkxp_sys::getRealHostType() == mkxp_sys::WineHostType::Linux);
}

RB_METHOD(mkxpIsReallyMacHost)
{
	RB_UNUSED_PARAM;
	return rb_bool_new(mkxp_sys::getRealHostType() == mkxp_sys::WineHostType::Mac);
}

RB_METHOD(mkxpUserLanguage)
{
	RB_UNUSED_PARAM;
	return rb_utf8_str_new_cstr(mkxp_sys::getSystemLanguage().c_str());
}

RB_METHOD(mkxpUserName)
{
	RB_UNUSED_PARAM;

#ifdef __WIN32__
	// Using the Windows API isn't working with usernames that involve Unicode
	// characters for some dumb reason.
	VALUE env = rb_const_get(rb_mKernel, rb_intern("ENV"));
	return rb_funcall(env, rb_intern("[]"), 1, rb_str_new_cstr("USERNAME"));
#else
	return rb_utf8_str_new_cstr(mkxp_sys::getUserName().c_str());
#endif
}

RB_METHOD(mkxpGameTitle) {
    RB_UNUSED_PARAM;
    
    return rb_utf8_str_new_cstr(shState->config().game.title.c_str());
}

RB_METHOD(mkxpPowerState) {
    RB_UNUSED_PARAM;
    
    int secs, pct;
    SDL_PowerState ps = SDL_GetPowerInfo(&secs, &pct);
    
    VALUE hash = rb_hash_new();
    
    rb_hash_aset(hash, ID2SYM(rb_intern("seconds")),
                 (secs > -1) ? INT2NUM(secs) : RUBY_Qnil);
    
    rb_hash_aset(hash, ID2SYM(rb_intern("percent")),
                 (pct > -1) ? INT2NUM(pct) : RUBY_Qnil);
    
    rb_hash_aset(hash, ID2SYM(rb_intern("discharging")),
                 rb_bool_new(ps == SDL_POWERSTATE_ON_BATTERY));
    
    return hash;
}

RB_METHOD(mkxpSettingsMenu) {
    RB_UNUSED_PARAM;
    
    shState->eThread().requestSettingsMenu();
    
    return Qnil;
}

RB_METHOD(mkxpCpuCount) {
    RB_UNUSED_PARAM;
    
    return INT2NUM(SDL_GetCPUCount());
}

RB_METHOD(mkxpSystemMemory) {
    RB_UNUSED_PARAM;
    
    return INT2NUM(SDL_GetSystemRAM());
}

RB_METHOD(mkxpReloadPathCache) {
    RB_UNUSED_PARAM;
    
    shState->fileSystem().reloadPathCache();
    return Qnil;
}

RB_METHOD(mkxpAddPath) {
    RB_UNUSED_PARAM;
    
    VALUE path, mountpoint, reload;
    rb_scan_args(argc, argv, "12", &path, &mountpoint, &reload);
    SafeStringValue(path);
    if (mountpoint != Qnil) SafeStringValue(mountpoint);
    
    const char *mp = (mountpoint == Qnil) ? 0 : RSTRING_PTR(mountpoint);
    
    try {
        bool rl = true;
        if (reload != Qnil)
            rb_bool_arg(reload, &rl);
        
        shState->fileSystem().addPath(RSTRING_PTR(path), mp, rl);
    } catch (Exception &e) {
        raiseRbExc(e);
    }
    return path;
}

RB_METHOD(mkxpRemovePath) {
    RB_UNUSED_PARAM;
    
    VALUE path, reload;
    rb_scan_args(argc, argv, "11", &path, &reload);
    SafeStringValue(path);
    
    try {
        bool rl = true;
        if (reload != Qnil)
            rb_bool_arg(reload, &rl);
        
        shState->fileSystem().removePath(RSTRING_PTR(path), rl);
    } catch (Exception &e) {
        raiseRbExc(e);
    }
    return path;
}

RB_METHOD(mkxpSetDefaultFontFamily) {
    RB_UNUSED_PARAM;
    
    VALUE familyV;
    rb_scan_args(argc, argv, "1", &familyV);
    SafeStringValue(familyV);
    
    std::string family(RSTRING_PTR(familyV));
    shState->fontState().setDefaultFontFamily(family);
    
    return Qnil;
}

RB_METHOD(mkxpStringToUTF8) {
    RB_UNUSED_PARAM;
    
    rb_check_argc(argc, 0);
    
    std::string ret(RSTRING_PTR(self), RSTRING_LEN(self));
    GUARD_EXC(ret = Encoding::convertString(ret); );
    
    return rb_utf8_str_new(ret.c_str(), ret.length());
}

RB_METHOD(mkxpStringToUTF8Bang) {
    RB_UNUSED_PARAM;
    
    rb_check_argc(argc, 0);
    
    std::string ret(RSTRING_PTR(self), RSTRING_LEN(self));
    GUARD_EXC(ret = Encoding::convertString(ret); );
    
    rb_str_resize(self, ret.length());
    memcpy(RSTRING_PTR(self), ret.c_str(), RSTRING_LEN(self));
    
#if RAPI_FULL >= 190
    rb_funcall(self, rb_intern("force_encoding"), 1, rb_enc_from_encoding(rb_utf8_encoding()));
#endif
    
    return self;
}

#ifdef __APPLE__
#define OPENCMD "open "
#define OPENARGS "--args"
#elif defined(__linux__)
#define OPENCMD "xdg-open "
#define OPENARGS ""
#else
#define OPENCMD "start /b \"launch\" "
#define OPENARGS ""
#endif

RB_METHOD(mkxpLaunch) {
    RB_UNUSED_PARAM;
    
    VALUE cmdname, args;
    
    rb_scan_args(argc, argv, "11", &cmdname, &args);
    SafeStringValue(cmdname);
    
    std::string command(OPENCMD);
    command += "\""; command += RSTRING_PTR(cmdname); command += "\"";
    
    if (args != RUBY_Qnil) {
#ifndef __linux__
        command += " ";
        command += OPENARGS;
        Check_Type(args, T_ARRAY);
        
        for (int i = 0; i < RARRAY_LEN(args); i++) {
            VALUE arg = rb_ary_entry(args, i);
            SafeStringValue(arg);
            
            if (RSTRING_LEN(arg) <= 0)
                continue;
            
            command += " ";
            command += RSTRING_PTR(arg);
        }
#else
        Debug() << command << ":" << "Arguments are not supported with xdg-open. Ignoring.";
#endif
    }
    
    if (std::system(command.c_str()) != 0) {
        raiseRbExc(Exception(Exception::MKXPError, "Failed to launch \"%s\"", RSTRING_PTR(cmdname)));
    }
    
    return RUBY_Qnil;
}

json5pp::value userSettings;

void loadUserSettings() {
    if (!userSettings.is_null())
        return;
    
    VALUE cpath = rb_utf8_str_new_cstr(shState->config().userConfPath.c_str());
    
    if (rb_funcall(rb_cFile, rb_intern("exists?"), 1, cpath) == Qtrue) {
        VALUE f = rb_funcall(rb_cFile, rb_intern("open"), 2, cpath, rb_str_new("r", 1));
        VALUE data = rb_funcall(f, rb_intern("read"), 0);
        rb_funcall(f, rb_intern("close"), 0);
        userSettings = rb2json(data);
    }
    
    if (!userSettings.is_object())
        userSettings = json5pp::object({});
}

void saveUserSettings() {
    VALUE cpath = rb_utf8_str_new_cstr(shState->config().userConfPath.c_str());
    VALUE f = rb_funcall(rb_cFile, rb_intern("open"), 2, cpath, rb_str_new("w", 1));
    rb_funcall(f, rb_intern("write"), 1, rb_utf8_str_new_cstr(userSettings.stringify5(json5pp::rule::space_indent<>()).c_str()));
    rb_funcall(f, rb_intern("close"), 0);
}

RB_METHOD(mkxpGetJSONSetting) {
    RB_UNUSED_PARAM;
    
    VALUE sname;
    rb_scan_args(argc, argv, "1", &sname);
    SafeStringValue(sname);
    
    loadUserSettings();
    auto &s = userSettings.as_object();
    
    if (s[RSTRING_PTR(sname)].is_null()) {
        return json2rb(shState->config().raw.as_object()[RSTRING_PTR(sname)]);
    }
    
    return json2rb(s[RSTRING_PTR(sname)]);
    
}

RB_METHOD(mkxpSetJSONSetting) {
    RB_UNUSED_PARAM;
    
    VALUE sname, svalue;
    rb_scan_args(argc, argv, "2", &sname, &svalue);
    SafeStringValue(sname);
    
    loadUserSettings();
    userSettings.as_object()[RSTRING_PTR(sname)] = rb2json(svalue);
    
    saveUserSettings();
    return Qnil;
}

RB_METHOD(mkxpGetAllJSONSettings) {
    RB_UNUSED_PARAM;
    
    return json2rb(shState->config().raw);
}

static VALUE rgssMainCb(VALUE block) {
    rb_funcall2(block, rb_intern("call"), 0, 0);
    return Qnil;
}

static VALUE rgssMainRescue(VALUE arg, VALUE exc) {
    VALUE *excRet = (VALUE *)arg;
    
    *excRet = exc;
    
    return Qnil;
}

static void processReset() {
    shState->graphics().reset();
    shState->audio().reset();
    
    shState->rtData().rqReset.clear();
    shState->graphics().repaintWait(shState->rtData().rqResetFinish, false);
}

RB_METHOD(mriRgssMain) {
    RB_UNUSED_PARAM;
    
    while (true) {
        VALUE exc = Qnil;
#if RAPI_FULL < 270
        rb_rescue2((VALUE(*)(ANYARGS))rgssMainCb, rb_block_proc(),
                   (VALUE(*)(ANYARGS))rgssMainRescue, (VALUE)&exc, rb_eException,
                   (VALUE)0);
#else
        rb_rescue2(rgssMainCb, rb_block_proc(), rgssMainRescue, (VALUE)&exc,
                   rb_eException, (VALUE)0);
#endif
        
        if (NIL_P(exc))
            break;
        
        if (rb_obj_class(exc) == getRbData()->exc[Reset])
            processReset();
        else
            rb_exc_raise(exc);
    }
    
    return Qnil;
}

RB_METHOD(mriRgssStop) {
    RB_UNUSED_PARAM;
    
    while (true)
        shState->graphics().update();
    
    return Qnil;
}

RB_METHOD(_kernelCaller) {
    RB_UNUSED_PARAM;
    
    VALUE trace =
    rb_funcall2(rb_mKernel, rb_intern("_mkxp_kernel_caller_alias"), 0, 0);
    
    if (!RB_TYPE_P(trace, RUBY_T_ARRAY))
        return trace;
    
    long len = RARRAY_LEN(trace);
    
    if (len < 2)
        return trace;
    
    /* Remove useless "ruby:1:in 'eval'" */
    rb_ary_pop(trace);
    
    /* Also remove trace of this helper function */
    rb_ary_shift(trace);
    
    len -= 2;
    
    if (len == 0)
        return trace;
    
    /* RMXP does this, not sure if specific or 1.8 related */
    VALUE args[] = {rb_utf8_str_new_cstr(":in `<main>'"), rb_utf8_str_new_cstr("")};
    rb_funcall2(rb_ary_entry(trace, len - 1), rb_intern("gsub!"), 2, args);
    
    return trace;
}

#if RAPI_FULL > 187
static VALUE newStringUTF8(const char *string, long length) {
    return rb_enc_str_new(string, length, rb_utf8_encoding());
}
#else
#define newStringUTF8 rb_str_new
#endif

struct evalArg {
    VALUE string;
    VALUE filename;
};

static VALUE evalHelper(evalArg *arg) {
    VALUE argv[] = {arg->string, Qnil, arg->filename};
    return rb_funcall2(Qnil, rb_intern("eval"), ARRAY_SIZE(argv), argv);
}

static VALUE evalString(VALUE string, VALUE filename, int *state) {
    evalArg arg = {string, filename};
    return rb_protect((VALUE(*)(VALUE))evalHelper, (VALUE)&arg, state);
}

static void runCustomScript(const std::string &filename) {
    std::string scriptData;
    
    if (!readFileSDL(filename.c_str(), scriptData)) {
        showMsg(std::string("Unable to open '") + filename + "'");
        return;
    }
    
    evalString(newStringUTF8(scriptData.c_str(), scriptData.size()),
               newStringUTF8(filename.c_str(), filename.size()), NULL);
}

VALUE kernelLoadDataInt(const char *filename, bool rubyExc, bool raw);

struct BacktraceData {
    /* Maps: Ruby visible filename, To: Actual script name */
    BoostHash<std::string, std::string> scriptNames;
};

bool evalScript(VALUE string, const char *filename)
{
    int state;
    evalString(string, rb_utf8_str_new_cstr(filename), &state);
    if (state) return false;
    return true;
}


#define SCRIPT_SECTION_FMT (rgssVer >= 3 ? "{%04ld}" : "Section%03ld")

static void runRMXPScripts(BacktraceData &btData) {
    const Config &conf = shState->rtData().config;
    const std::string &scriptPack = conf.game.scripts;
    
    if (scriptPack.empty()) {
        showMsg("No game scripts specified (missing Game.ini?)");
        return;
    }
    
    if (!shState->fileSystem().exists(scriptPack.c_str())) {
        showMsg("Unable to open '" + scriptPack + "'");
        return;
    }
    
    VALUE scriptArray;
    
    /* We checked if Scripts.rxdata exists, but something might
     * still go wrong */
    try {
        scriptArray = kernelLoadDataInt(scriptPack.c_str(), false, false);
    } catch (const Exception &e) {
        showMsg(std::string("Failed to read script data: ") + e.msg);
        return;
    }
    
    if (!RB_TYPE_P(scriptArray, RUBY_T_ARRAY)) {
        showMsg("Failed to read script data");
        return;
    }
    
    rb_gv_set("$RGSS_SCRIPTS", scriptArray);
    
    long scriptCount = RARRAY_LEN(scriptArray);
    
    std::string decodeBuffer;
    decodeBuffer.resize(0x1000);
    
    for (long i = 0; i < scriptCount; ++i) {
        VALUE script = rb_ary_entry(scriptArray, i);
        
        if (!RB_TYPE_P(script, RUBY_T_ARRAY))
            continue;
        
        VALUE scriptName = rb_ary_entry(script, 1);
        VALUE scriptString = rb_ary_entry(script, 2);
        
        int result = Z_OK;
        unsigned long bufferLen;
        
        while (true) {
            unsigned char *bufferPtr = reinterpret_cast<unsigned char *>(
                                                                         const_cast<char *>(decodeBuffer.c_str()));
            const unsigned char *sourcePtr =
            reinterpret_cast<const unsigned char *>(RSTRING_PTR(scriptString));
            
            bufferLen = decodeBuffer.length();
            
            result = uncompress(bufferPtr, &bufferLen, sourcePtr,
                                RSTRING_LEN(scriptString));
            
            bufferPtr[bufferLen] = '\0';
            
            if (result != Z_BUF_ERROR)
                break;
            
            decodeBuffer.resize(decodeBuffer.size() * 2);
        }
        
        if (result != Z_OK) {
            static char buffer[256];
            snprintf(buffer, sizeof(buffer), "Error decoding script %ld: '%s'", i,
                     RSTRING_PTR(scriptName));
            
            showMsg(buffer);
            
            break;
        }
        
        rb_ary_store(script, 3, rb_utf8_str_new_cstr(decodeBuffer.c_str()));
    }
    
    /* Execute preloaded scripts */
    for (std::vector<std::string>::const_iterator i = conf.preloadScripts.begin();
         i != conf.preloadScripts.end(); ++i)
        runCustomScript(*i);
    
    VALUE exc = rb_gv_get("$!");
    if (exc != Qnil)
        return;
    
    while (true) {
        for (long i = 0; i < scriptCount; ++i) {
            VALUE script = rb_ary_entry(scriptArray, i);
            VALUE scriptDecoded = rb_ary_entry(script, 3);
            VALUE string =
            newStringUTF8(RSTRING_PTR(scriptDecoded), RSTRING_LEN(scriptDecoded));
            
            VALUE fname;
            const char *scriptName = RSTRING_PTR(rb_ary_entry(script, 1));
            char buf[512];
            int len;
            
            if (conf.useScriptNames)
                len = snprintf(buf, sizeof(buf), "%03ld:%s", i, scriptName);
            else
                len = snprintf(buf, sizeof(buf), SCRIPT_SECTION_FMT, i);
            
            fname = newStringUTF8(buf, len);
            btData.scriptNames.insert(buf, scriptName);
            
            
            // if the script name starts with |s|, only execute
            // it if "s" is the same first letter as the platform
            // we're running on
            
            // |W| - Windows, |M| - Mac OS X, |L| - Linux
            
            // Adding a 'not' symbol means it WON'T run on that
            // platform (i.e. |!W| won't run on Windows)
            /*
             if (scriptName[0] == '|') {
             int len = strlen(scriptName);
             if (len > 2) {
             if (scriptName[1] == '!' && len > 3 &&
             scriptName[3] == scriptName[0]) {
             if (toupper(scriptName[2]) == platform[0])
             continue;
             }
             if (scriptName[2] == scriptName[0] &&
             toupper(scriptName[1]) != platform[0])
             continue;
             }
             }
             */
            
            int state;
            
#ifdef __ANDROID__
            // AnyPlay: old RPG Maker games were written for Ruby 1.8, which
            // allowed "Foo.new (a, b)" (space before the bracket). Modern Ruby
            // rejects that as a SyntaxError, so remove the space first.
            // Also: the byte range \x7f-\x9f in a regex is invalid in UTF-8 source,
            // so write it as the matching unicode range instead.
            {
                static VALUE compatFix = Qnil;
                if (compatFix == Qnil) {
                    rb_eval_string(R"RB(
# Ruby 1.8 accepted `return` inside a top-level `proc{ ... }`; Ruby 3 raises
# LocalJumpError. Rewrite `return` (bare or before if/unless) in such procs to
# a catch/throw pair, without changing line numbers.
$__anyplay_fix_proc_return = lambda { |s|
  next s unless s.include?("return") && s =~ /\b(?:proc|Proc\.new)\s*\{/
  out = +""
  pos = 0
  while (m = s.match(/\b(?:proc|Proc\.new)\s*\{/, pos))
    open = m.end(0)
    depth = 1
    i = open
    n = s.length
    q = nil
    while i < n && depth > 0
      c = s[i]
      if q
        if c == "\\" then i += 1
        elsif c == q then q = nil
        end
      elsif c == '"' || c == "'"
        q = c
      elsif c == "#"
        i += 1 while i < n && s[i] != "\n"
        next
      elsif c == "{"
        depth += 1
      elsif c == "}"
        depth -= 1
      end
      i += 1
    end
    if depth != 0
      out << s[pos...m.end(0)]
      pos = m.end(0)
      next
    end
    close = i - 1
    body = s[open...close]
    out << s[pos...open]
    # only procs registered at top level (statement starts in column 0): inside
    # a method `return` in a proc legitimately returns from that method.
    line_start = (s.rindex("\n", m.begin(0) - 1) || -1) + 1
    top_level = !(s[line_start] == " " || s[line_start] == "\t")
    if top_level && body =~ /\breturn\b/ && body !~ /\bdef\s/
      last_line = body[/[^\n]*\z/]
      if last_line.include?("#")
        out << body
      else
        params = body[/\A\s*\|[^|]*\|/] || ""
        rest = body[params.length..-1]
        rest = rest.gsub(/\breturn\b(?=[ \t]*(?:if\b|unless\b|;|$|\}))/, "throw :__ap_ret")
        out << params << " catch(:__ap_ret){ " << rest << (last_line.strip.empty? ? "" : " ") << "}"
      end
    else
      out << body
    end
    pos = close
  end
  out << s[pos..-1]
  out
}
)RB");
                    compatFix = rb_eval_string(R"RB(
lambda { |s|
  begin
    s = $__anyplay_fix_proc_return.call(s) if $__anyplay_fix_proc_return
    s = s.gsub(/\.new[ \t]+\(/, '.new(')
    s = s.gsub('\x7f-\x9f') { '\u007f-\u009f' }
    s
  rescue Exception
    s
  end
}
)RB");
                    rb_gc_register_address(&compatFix);
                }
                VALUE fixed = rb_funcall(compatFix, rb_intern("call"), 1, string);
                if (fixed != Qnil)
                    string = fixed;
            }
#endif
            evalString(string, fname, &state);
            if (state)
                break;
        }
        
        VALUE exc = rb_gv_get("$!");
        if (rb_obj_class(exc) != getRbData()->exc[Reset])
            break;
        
        processReset();
    }
}

static void showExc(VALUE exc, const BacktraceData &btData) {
    VALUE bt = rb_funcall2(exc, rb_intern("backtrace"), 0, NULL);
    VALUE msg = rb_funcall2(exc, rb_intern("message"), 0, NULL);
    VALUE bt0 = rb_ary_entry(bt, 0);
    VALUE name = rb_class_path(rb_obj_class(exc));
    
    VALUE ds = rb_sprintf("%" PRIsVALUE ": %" PRIsVALUE " (%" PRIsVALUE ")",
#if RAPI_MAJOR >= 2
                          bt0, exc, name);
#else
    // Ruby 1.9's version of this function needs char*
    RSTRING_PTR(bt0), RSTRING_PTR(exc), RSTRING_PTR(name));
#endif
    /* omit "useless" last entry (from ruby:1:in `eval') */
    for (long i = 1, btlen = RARRAY_LEN(bt) - 1; i < btlen; ++i)
        rb_str_catf(ds, "\n\tfrom %" PRIsVALUE,
#if RAPI_MAJOR >= 2
                    rb_ary_entry(bt, i));
#else
    RSTRING_PTR(rb_ary_entry(bt, i)));
#endif
    Debug() << StringValueCStr(ds);
    
    char *s = RSTRING_PTR(bt0);
    
    char line[16];
    std::string file(512, '\0');
    
    char *p = s + strlen(s);
    char *e;
    
    while (p != s)
        if (*--p == ':')
            break;
    
    e = p;
    
    while (p != s)
        if (*--p == ':')
            break;
    
    /* s         p  e
     * SectionXXX:YY: in 'blabla' */
    
    *e = '\0';
    strncpy(line, *p ? p + 1 : p, sizeof(line));
    line[sizeof(line) - 1] = '\0';
    *e = ':';
    e = p;
    
    /* s         e
     * SectionXXX:YY: in 'blabla' */
    
    *e = '\0';
    strncpy(&file[0], s, file.size());
    *e = ':';
    
    /* Shrink to fit */
    file.resize(strlen(file.c_str()));
    file = btData.scriptNames.value(file, file);
    
    std::string ms(640, '\0');
    snprintf(&ms[0], ms.size(), "Script '%s' line %s: %s occured.\n\n%s",
             file.c_str(), line, RSTRING_PTR(name), RSTRING_PTR(msg));
    
    showMsg(ms);
}

static void mriBindingExecute() {
    Config &conf = shState->rtData().config;
    
#if RAPI_MAJOR >= 2
    /* Normally only a ruby executable would do a sysinit,
     * but not doing it will lead to crashes due to closed
     * stdio streams on some platforms (eg. Windows) */
    int argc = 0;
    char **argv = 0;
    ruby_sysinit(&argc, &argv);
    
    RUBY_INIT_STACK;
    ruby_init();
    
    std::vector<const char*> rubyArgsC{"mkxp-z"};
    rubyArgsC.push_back("-e ");
    void *node;
    if (conf.jit.enabled) {
        std::string verboseLevel("--jit-verbose="); verboseLevel += std::to_string(conf.jit.verboseLevel);
        std::string maxCache("--jit-max-cache="); maxCache += std::to_string(conf.jit.maxCache);
        std::string minCalls("--jit-min-calls="); minCalls += std::to_string(conf.jit.minCalls);
        rubyArgsC.push_back("--jit");
        rubyArgsC.push_back(verboseLevel.c_str());
        rubyArgsC.push_back(maxCache.c_str());
        rubyArgsC.push_back(minCalls.c_str());
        node = ruby_options(rubyArgsC.size(), const_cast<char**>(rubyArgsC.data()));
    } else {
        node = ruby_options(rubyArgsC.size(), const_cast<char**>(rubyArgsC.data()));
    }
    
    int state;
    bool valid = ruby_executable_node(node, &state);
    if (valid)
        state = ruby_exec_node(node);
    if (state || !valid) {
        // The message is formatted for and automatically spits
        // out to the terminal, so let's leave it that way for now
        /*
         VALUE exc = rb_errinfo();
         #if RAPI_FULL >= 250
         VALUE msg = rb_funcall(exc, rb_intern("full_message"), 0);
         #else
         VALUE msg = rb_funcall(exc, rb_intern("message"), 0);
         #endif
         */
        showMsg("An error occurred while initializing Ruby. (Invalid JIT settings?)");
        ruby_cleanup(state);
        shState->rtData().rqTermAck.set();
        return;
    }
    rb_enc_set_default_internal(rb_enc_from_encoding(rb_utf8_encoding()));
    rb_enc_set_default_external(rb_enc_from_encoding(rb_utf8_encoding()));
#else
    ruby_init();
    rb_eval_string("$KCODE='U'");
#ifdef __WIN32__
    if (!conf.winConsole) {
        VALUE iostr = rb_str_new2("NUL");
        // Sysinit isn't a thing yet, so send io to /dev/null instead
        rb_funcall(rb_gv_get("$stderr"), rb_intern("reopen"), 1, iostr);
        rb_funcall(rb_gv_get("$stdout"), rb_intern("reopen"), 1, iostr);
    }
#endif
#endif
    
    VALUE rbArgv = rb_get_argv();
    for (const auto &str : conf.launchArgs)
        rb_ary_push(rbArgv, rb_utf8_str_new_cstr(str.c_str()));
    
    // Duplicates get pushed for some reason
    rb_funcall(rbArgv, rb_intern("uniq!"), 0);
    
    VALUE lpaths = rb_gv_get(":");
    rb_ary_clear(lpaths);
    
#if defined(MKXPZ_BUILD_XCODE) && RAPI_MAJOR >= 2
    std::string resPath = mkxp_fs::getResourcePath();
    resPath += "/Ruby/" + std::to_string(RAPI_MAJOR) + "." + std::to_string(RAPI_MINOR) + ".0";
    rb_ary_push(lpaths, rb_str_new(resPath.c_str(), resPath.size()));
#endif
    
    if (!conf.rubyLoadpaths.empty()) {
        /* Setup custom load paths */
        for (size_t i = 0; i < conf.rubyLoadpaths.size(); ++i) {
            std::string &path = conf.rubyLoadpaths[i];
            
            VALUE pathv = rb_str_new(path.c_str(), path.size());
            rb_ary_push(lpaths, pathv);
        }
    }
#ifndef WORKDIR_CURRENT
    else {
        rb_ary_push(lpaths, rb_utf8_str_new_cstr(mkxp_fs::getCurrentDirectory().c_str()));
    }
#endif
    
    RbData rbData;
    shState->setBindingData(&rbData);
    BacktraceData btData;
    
    mriBindingInit();
    
#ifdef __ANDROID__
    // AnyPlay: RPG Maker XP/VX/Ace ship with Zlib (and a few other standard
    // libraries) already loaded, so games use them without "require".
    // Load them up front; if one is missing, just carry on.
    {
        int reqState = 0;
        rb_eval_string_protect(R"RB(
['zlib', 'stringio'].each { |l| begin; require l; rescue LoadError; end }

# Things Ruby 1.8 had and modern Ruby removed, which old RPG Maker scripts still use.
class Thread
  @__anyplay_critical = false
  class << self
    unless respond_to?(:critical)
      def critical; @__anyplay_critical; end
      def critical=(v); @__anyplay_critical = v; end
    end
  end
end
class Array
  def nitems; count { |x| !x.nil? }; end unless method_defined?(:nitems)
end
class << File
  alias_method :exists?, :exist? unless respond_to?(:exists?)
end
class << Dir
  alias_method :exists?, :exist? unless respond_to?(:exists?)
end
Fixnum = Integer unless defined?(Fixnum)
Bignum = Integer unless defined?(Bignum)
)RB", &reqState);
        if (reqState)
            rb_set_errinfo(Qnil);

        // AnyPlay: safe dispose, full-screen fit, tap-to-walk (see anyplay_prelude.rb)
        {
            int preState = 0;
            rb_eval_string_protect(R"RB# AnyPlay RGSS plugin: runs before the game's own scripts (Android only).
# 1) Plane/Sprite/... #dispose no longer crashes for subclasses that skip `super` in initialize
# 2) game screen is resized to the game's own size (fills the screen)
# 3) touch: tap = walk there / talk to NPC / confirm
module AnyPlayCompat
  def self.safe_dispose_patch
    [:Plane, :Sprite, :Viewport, :Window, :Tilemap].each do |n|
      begin
        k = Object.const_get(n)
        next unless k.method_defined?(:dispose)
        next if k.method_defined?(:__anyplay_dispose)
        k.class_eval do
          alias_method :__anyplay_dispose, :dispose
          def dispose
            __anyplay_dispose
          rescue Exception => e
            raise unless e.message.to_s.include?("No instance data")
            nil
          end
        end
      rescue Exception
      end
    end
  end
end
AnyPlayCompat.safe_dispose_patch

# Windows environment variables that games expect (TEMP for temp files, APPDATA
# as a fallback save folder). On Android they are missing, so scripts crashed
# with "no implicit conversion of nil into String".
begin
  __ap_base = "/data/data/com.anyplay.rgssplugin"
  __ap_tmp = __ap_base + "/cache"
  __ap_files = __ap_base + "/files"
  [__ap_tmp, __ap_files].each { |d| (Dir.mkdir(d) unless File.directory?(d)) rescue nil }
  %w(TEMP TMP TMPDIR).each { |k| ENV[k] = __ap_tmp if ENV[k].nil? || ENV[k].empty? }
  %w(APPDATA LOCALAPPDATA).each { |k| ENV[k] = __ap_files if ENV[k].nil? || ENV[k].empty? }
rescue Exception
end

# Windows raises EACCES when you open a folder as a file; Linux raises EISDIR later
# on read. Many scripts only rescue the Windows errors, so behave like Windows.
class << File
  alias_method :__anyplay_open, :open
  def open(path, *args, &blk)
    if path.is_a?(String) && File.directory?(path)
      mode = args[0]
      if mode.nil? || (mode.is_a?(String) && mode.start_with?("r"))
        raise Errno::EACCES, path
      end
    end
    __anyplay_open(path, *args, &blk)
  end
end

module AnyPlayLog
  @n = 0
  def self.write(msg)
    return if @n > 60
    @n += 1
    File.__anyplay_open("AnyPlay_touch_log.txt", "a") { |f| f.puts(msg) }
  rescue Exception
  end
end

module AnyPlayTouch
  DIRS = { 2 => [0, 1], 4 => [-1, 0], 6 => [1, 0], 8 => [0, -1] }
  @route = []          # list of directions still to walk
  @face = nil          # direction to face at the end (to talk to an NPC)
  @dir = nil           # direction handed to the game this frame
  @press_c = 0
  @tap_seq = nil
  @stuck = 0
  @hooked = false
  @errors = 0
  @sized = false
  @native_resize = nil
  class << self
    attr_accessor :dir, :press_c
    attr_writer :native_resize

    def tile_w
      (Game_Map::TILEWIDTH rescue 32)
    end

    def tile_h
      (Game_Map::TILEHEIGHT rescue 32)
    end

    def scene_is_map?
      sc = (defined?(SceneManager) ? SceneManager.scene : $scene)
      return false if sc.nil?
      return true if defined?(Scene_Map) && sc.is_a?(Scene_Map)
      sc.class.name.to_s.include?("Map") || sc.instance_variable_defined?(:@spriteset)
    rescue Exception
      false
    end

    def map_free?
      return false unless scene_is_map?
      return false if ($game_temp.message_window_showing rescue false)
      return false if ($game_message.visible rescue false)
      return false if ($game_system.map_interpreter.running? rescue false)
      return false if ($game_map.interpreter.running? rescue false)
      true
    end

    def mouse_down?
      @gaks ||= Win32API.new('user32', 'GetAsyncKeyState', 'i', 'i')
      (@gaks.call(1) & 0x8000) != 0
    rescue Exception
      false
    end

    def hook_input
      return if @hooked
      return unless defined?(Input) && Input.respond_to?(:dir4)
      @hooked = true
      class << Input
        alias_method :__ap_dir4, :dir4
        def dir4
          d = AnyPlayTouch.dir
          return d if d
          __ap_dir4
        end
        alias_method :__ap_trigger, :trigger?
        def trigger?(b)
          if AnyPlayTouch.press_c > 0 && (b == Input::C)
            AnyPlayTouch.press_c = 0
            AnyPlayLog.write("  confirm sent")
            return true
          end
          __ap_trigger(b)
        end
        if Input.respond_to?(:repeat?)
          alias_method :__ap_repeat, :repeat?
          def repeat?(b)
            if AnyPlayTouch.press_c > 0 && (b == Input::C)
              AnyPlayTouch.press_c = 0
              return true
            end
            __ap_repeat(b)
          end
        end
      end
    end

    def screen_to_tile(mx, my)
      f = ($ResizeFactor || 1.0).to_f
      f = 1.0 if f <= 0
      ox = ($ResizeOffsetX || 0).to_f
      oy = ($ResizeOffsetY || 0).to_f
      tw = tile_w * f
      th = tile_h * f
      psx = ($game_player.screen_x + ox) * f
      psy = ($game_player.screen_y + oy) * f - th / 2.0
      [$game_player.x + ((mx - psx) / tw).round, $game_player.y + ((my - psy) / th).round]
    end

    def event_at(x, y)
      evs = $game_map.events
      list = evs.respond_to?(:values) ? evs.values : evs
      list.find do |e|
        next false unless e.x == x && e.y == y && !e.through
        e.respond_to?(:list) ? (e.list && e.list.length > 1) : true
      end
    rescue Exception
      nil
    end

    def can_step?(x, y, d)
      $game_player.passable?(x, y, d)
    rescue Exception
      false
    end

    def plan(tx, ty)
      sx = $game_player.x
      sy = $game_player.y
      start = [sx, sy]
      prev = { start => nil }
      queue = [start]
      i = 0
      while i < queue.length && prev.length < 5000
        cur = queue[i]; i += 1
        DIRS.each do |d, (dx, dy)|
          nx = cur[0] + dx
          ny = cur[1] + dy
          next if prev.key?([nx, ny])
          next unless $game_map.valid?(nx, ny)
          next unless can_step?(cur[0], cur[1], d)
          prev[[nx, ny]] = [cur, d]
          queue << [nx, ny]
        end
      end
      ev = event_at(tx, ty)
      goal = nil
      face = nil
      if ev && prev.length > 1
        # walk next to the NPC/object and face it
        best = nil
        DIRS.each do |d, (dx, dy)|
          n = [tx - dx, ty - dy]
          next unless prev.key?(n)
          len = path_len(prev, n)
          best = [n, d, len] if best.nil? || len < best[2]
        end
        if best
          goal = best[0]; face = best[1]
        end
      end
      if goal.nil?
        if prev.key?([tx, ty])
          goal = [tx, ty]
        else
          goal = queue.min_by { |n| [(n[0] - tx).abs + (n[1] - ty).abs, path_len(prev, n)] }
        end
      end
      dirs = []
      n = goal
      while prev[n]
        dirs.unshift(prev[n][1])
        n = prev[n][0]
      end
      [dirs, face]
    end

    def path_len(prev, n)
      c = 0
      while prev[n]
        n = prev[n][0]; c += 1
      end
      c
    end

    def turn(d)
      case d
      when 2 then $game_player.turn_down
      when 4 then $game_player.turn_left
      when 6 then $game_player.turn_right
      when 8 then $game_player.turn_up
      end
    rescue Exception
    end

    def on_tap(mx, my)
      @route = []; @face = nil; @dir = nil; @expect = nil
      AnyPlayLog.write("tap #{mx},#{my} scene=#{(defined?(SceneManager) ? SceneManager.scene : $scene).class} free=#{map_free?} player=#{($game_player.x rescue '?')},#{($game_player.y rescue '?')}")
      unless map_free?
        @press_c = 4          # dialogs, menus, title: tap = confirm
        return
      end
      tx, ty = screen_to_tile(mx, my)
      if tx == $game_player.x && ty == $game_player.y
        @press_c = 4
        return
      end
      route, face = plan(tx, ty)
      AnyPlayLog.write("  target=#{tx},#{ty} route=#{route.length} face=#{face.inspect}")
      if route.empty?
        if face
          turn(face); @press_c = 4
        elsif (ev = event_at(tx, ty))
          @press_c = 4
        end
        return
      end
      @route = route
      @face = face
      @stuck = 0
    rescue Exception
      @route = []
    end

    def walk
      if !map_free?
        @route = []; @face = nil; @dir = nil; @expect = nil
        return
      end
      if @route.empty? && !@expect
        @dir = nil
        return
      end
      if $game_player.moving?
        @dir = nil
        return
      end
      if @expect
        if $game_player.x == @expect[0] && $game_player.y == @expect[1]
          @route.shift
          @stuck = 0
        else
          @stuck += 1
        end
        @expect = nil
        if @stuck > 15
          @route = []; @face = nil; @dir = nil; @stuck = 0
          return
        end
      end
      if @route.empty?
        @dir = nil
        if @face
          turn(@face); @face = nil; @press_c = 4
        end
        return
      end
      d = @route.first
      @dir = d
      @expect = [$game_player.x + DIRS[d][0], $game_player.y + DIRS[d][1]]
    end

    def tick
      hook_input
      if @press_c > 0
        @press_c -= 1
      end
      if Input.respond_to?(:anyplay_tap_seq)
        seq = Input.anyplay_tap_seq
        if @tap_seq.nil?
          @tap_seq = seq
        elsif seq != @tap_seq
          @tap_seq = seq
          on_tap(Input.anyplay_tap_x, Input.anyplay_tap_y)
        end
      end
      if defined?($game_player) && $game_player
        # any real direction key cancels the walk
        if @route.any? && !@dir
          real = (Input.__ap_dir4 rescue 0)
          if real && real > 0
            @route = []; @face = nil
          end
        end
        walk
      else
        @dir = nil
      end
    end

    def fit_screen
      return if @sized
      return unless defined?(DEFAULTSCREENWIDTH) && defined?(DEFAULTSCREENHEIGHT)
      @sized = true
      return unless @native_resize
      @native_resize.call(DEFAULTSCREENWIDTH, DEFAULTSCREENHEIGHT)
    rescue Exception
    end

    def frame
      return if @errors > 20
      fit_screen
      tick
    rescue Exception
      @errors += 1
      @route = []; @dir = nil
    end
  end
end

# ---- AnyPlay cheats (RPG Maker XP / VX / Ace and Pokemon Essentials) --------
# The Android overlay writes one command per file into the app cache folder,
# this module reads it (every few frames), runs it, and writes a one line
# answer back.  Checkbox cheats are just flags that are applied every frame.
module AnyPlayCheats
  DIR   = "/data/data/com.anyplay.rgssplugin/cache"
  CMD   = DIR + "/anyplay_cheat_cmd.txt"
  REPLY = DIR + "/anyplay_cheat_reply.txt"
  NOPE  = "This cheat doesn't work in this type of game."
  NEED_BATTLE = "You are not in a battle right now."

  @on = {}
  @speed = 2.0
  @engine = nil
  @tick = 0
  @errors = 0
  @hooked = false
  @item_snap = nil
  @old_textspeed = nil
  @through_forced = false
  @battle = nil

  class << self
    attr_accessor :battle

    # ---------------------------------------------------------------- engine
    def engine
      return @engine if @engine
      if defined?(PokeBattle_Battle) && defined?(PokemonBag) && defined?(PokeBattle_Pokemon)
        @engine = :essentials
      elsif defined?(Game_Actor) && defined?(Game_Party)
        m = Game_Actor.instance_methods
        @engine = if m.include?(:tp) && m.include?(:mp) then :ace
                  elsif m.include?(:mp) then :vx
                  else :xp end
      else
        return :unknown
      end
      @engine
    rescue Exception
      :unknown
    end

    def ess?; engine == :essentials; end
    def rpgm?; [:xp, :vx, :ace].include?(engine); end

    def on?(name); @on[name] ? true : false; end
    def speed_on?; @on['speed'] ? true : false; end
    def speed_bonus
      m = @speed.to_f
      m = 1.0 if m <= 0
      Math.log(m) / Math.log(2)
    end

    # ------------------------------------------------------- party helpers
    def actors
      p = $game_party
      return [] unless p
      list = if p.respond_to?(:members) then p.members
             elsif p.respond_to?(:actors) then p.actors
             else [] end
      list.compact
    rescue Exception
      []
    end

    def pokes
      return [] unless $Trainer && $Trainer.respond_to?(:party)
      ($Trainer.party || []).compact
    rescue Exception
      []
    end

    def in_battle?
      case engine
      when :essentials then !@battle.nil?
      when :ace then ($game_party && $game_party.respond_to?(:in_battle?) && $game_party.in_battle?) ? true : false
      else ($game_temp && $game_temp.respond_to?(:in_battle) && $game_temp.in_battle) ? true : false
      end
    rescue Exception
      false
    end

    def maxhp_of(a); a.respond_to?(:mhp) ? a.mhp : a.maxhp; end
    def maxmp_of(a)
      return a.mmp if a.respond_to?(:mmp)
      return a.maxsp if a.respond_to?(:maxsp)
      a.maxmp
    end

    # ------------------------------------------------------------- commands
    # Returns the text that is shown to the player as a toast.
    def handle(cmd, a = "", b = "")
      case cmd
      when 'restore' then restore(a, b)
      when 'toggle'  then toggle(a, b == '1')
      when 'value'   then set_value(a, b)
      when 'give'    then give(a, b)
      when 'run'     then run_action(a)
      when 'list'    then list_items(a)
      else "Unknown cheat command."
      end
    rescue Exception => e
      "Cheat failed: #{e.class}: #{e.message[0, 80]}"
    end

    def restore(speed, names)
      s = speed.to_f
      @speed = s if s > 0
      @on = {}
      names.to_s.split(',').each { |n| @on[n] = true unless n.empty? }
      @item_snap = nil
      ""
    end

    def toggle(name, enable)
      unless toggle_supported?(name)
        @on.delete(name)
        return "#{NOPE}"
      end
      if enable
        @on[name] = true
        @item_snap = nil if name == 'items'
      else
        @on.delete(name)
        toggle_off(name)
      end
      ""
    end

    def toggle_supported?(name)
      case name
      when 'tp'                         then engine == :ace
      when 'fastText'                   then ess?
      when 'oneHit', 'invincible', 'crit', 'escape', 'freeSkill' then ess?
      when 'hpmp', 'gold', 'status', 'items', 'noclip', 'noEncounter', 'speed'
        ess? || rpgm?
      else false
      end
    end

    def toggle_off(name)
      case name
      when 'noclip'
        $game_player.through = false if $game_player && @through_forced
        @through_forced = false
      when 'fastText'
        if @old_textspeed && defined?($PokemonSystem) && $PokemonSystem
          $PokemonSystem.textspeed = @old_textspeed
        end
        @old_textspeed = nil
      when 'items'
        @item_snap = nil
      end
    end

    # ------------------------------------------------------------- values
    def set_value(name, v)
      if name == 'speed'
        s = v.to_f
        return "Enter a number above 0." unless s > 0
        @speed = [[s, 0.25].max, 8.0].min
        @on['speed'] = true
        return "Speed x#{@speed}"
      end
      n = v.to_s.strip
      return "Enter a number first." if n.empty?
      n = n.to_i
      return "Enter a number first." if n < 0
      case engine
      when :essentials then ess_value(name, n)
      when :xp, :vx, :ace then rpgm_value(name, n)
      else NOPE
      end
    end

    def ess_value(name, n)
      case name
      when 'gold'
        return "Trainer data isn't ready yet. Start or load a game first." unless $Trainer
        $Trainer.money = n
        "Money set to #{$Trainer.money}"
      when 'level'
        ps = pokes
        return "You have no Pokemon yet." if ps.empty?
        lv = [[n, 1].max, PBExperience::MAXLEVEL].min
        ps.each do |p|
          next if p.egg?
          p.level = lv
          p.calcStats
        end
        "All Pokemon set to level #{lv}"
      when 'exp'
        ps = pokes
        return "You have no Pokemon yet." if ps.empty?
        ps.each do |p|
          next if p.egg?
          mx = PBExperience.pbGetMaxExperience(p.growthrate)
          p.exp = [n, mx].min
          p.calcStats
        end
        "EXP set for all Pokemon"
      else
        NOPE
      end
    end

    def rpgm_value(name, n)
      return "Start or load a game first." unless $game_party
      case name
      when 'gold'
        $game_party.instance_variable_set(:@gold, [n, 99_999_999].min)
        "Gold set to #{n}"
      when 'level'
        ac = actors
        return "Your party is empty." if ac.empty?
        ac.each do |a|
          if a.respond_to?(:change_level)
            a.change_level(n, false)
          else
            a.level = n
          end
        end
        "Party level set to #{n}"
      when 'exp'
        ac = actors
        return "Your party is empty." if ac.empty?
        ac.each do |a|
          if a.respond_to?(:change_exp)
            a.change_exp(n, false)
          else
            a.exp = n
          end
        end
        "Party EXP set to #{n}"
      when 'atk', 'def', 'mat', 'mdf', 'agi', 'luk'
        set_stat(name, n)
      else
        NOPE
      end
    end

    def set_stat(name, n)
      ac = actors
      return "Your party is empty." if ac.empty?
      done = false
      ac.each do |a|
        if engine == :ace
          id = { 'atk' => 2, 'def' => 3, 'mat' => 4, 'mdf' => 5, 'agi' => 6, 'luk' => 7 }[name]
          a.add_param(id, n - a.param(id))
          done = true
        elsif engine == :vx
          k = { 'atk' => :atk, 'def' => :def, 'mat' => :spi, 'agi' => :agi }[name]
          next unless k
          cur = a.send(k)
          a.send("#{k}_plus=", a.send("#{k}_plus") + (n - cur))
          done = true
        else
          k = { 'atk' => :str, 'mat' => :int, 'agi' => :agi, 'luk' => :dex }[name]
          next unless k
          cur = a.send(k)
          a.send("#{k}_plus=", a.send("#{k}_plus") + (n - cur))
          done = true
        end
      end
      done ? "Stat set to #{n} for the whole party" : NOPE
    end

    # -------------------------------------------------------------- items
    def give(kind, arg)
      id_s, qty_s = arg.to_s.split("\t", 2)
      id = id_s.to_i
      qty = qty_s.to_s.strip.empty? ? 1 : qty_s.to_i
      return "Enter an item ID (or use Pick)." if id <= 0
      return "Quantity must be 1 or more." if qty < 1
      case engine
      when :essentials
        return NOPE unless kind == 'item'
        return "Bag isn't ready yet. Start or load a game first." unless $PokemonBag
        ok = $PokemonBag.pbStoreItem(id, qty)
        ok ? "Added #{qty} x #{PBItems.getName(id)}" : "Your bag is full or doesn't accept this item."
      when :xp, :vx, :ace
        return "Start or load a game first." unless $game_party
        if engine == :xp
          case kind
          when 'item'   then $game_party.gain_item(id, qty)
          when 'weapon' then $game_party.gain_weapon(id, qty)
          when 'armor'  then $game_party.gain_armor(id, qty)
          end
        else
          obj = case kind
                when 'item'   then $data_items[id]
                when 'weapon' then $data_weapons[id]
                when 'armor'  then $data_armors[id]
                end
          return "That ID doesn't exist in this game." unless obj
          $game_party.gain_item(obj, qty)
        end
        "Added #{qty} x #{item_name(kind, id)}"
      else
        NOPE
      end
    end

    def table_for(kind)
      case kind
      when 'item'   then $data_items
      when 'weapon' then $data_weapons
      when 'armor'  then $data_armors
      end
    end

    def item_name(kind, id)
      t = table_for(kind)
      (t && t[id]) ? t[id].name.to_s : "item #{id}"
    rescue Exception
      "item #{id}"
    end

    # Answer is "id<TAB>name" lines, one per entry.
    def list_items(kind)
      out = []
      case engine
      when :essentials
        return "" unless kind == 'item'
        top = (PBItems.maxValue rescue 0)
        (1..top).each do |i|
          n = (PBItems.getName(i) rescue "").to_s
          out << "#{i}\t#{n}" unless n.empty?
        end
      when :xp, :vx, :ace
        t = table_for(kind)
        return "" unless t
        (1...t.size).each do |i|
          o = t[i]
          next unless o
          n = o.name.to_s
          out << "#{i}\t#{n}" unless n.empty?
        end
      end
      out.join("\n")
    end

    # ------------------------------------------------------ one-click actions
    def run_action(name)
      case name
      when 'maxExp'        then max_exp
      when 'instantWin'    then instant_win
      when 'fullInventory' then full_inventory
      else "Unknown action."
      end
    end

    def max_exp
      case engine
      when :essentials
        ps = pokes
        return "You have no Pokemon yet." if ps.empty?
        ps.each do |p|
          next if p.egg?
          p.level = PBExperience::MAXLEVEL
          p.calcStats
        end
        "All Pokemon are now at the maximum level"
      when :xp, :vx, :ace
        ac = actors
        return "Your party is empty." if ac.empty?
        ac.each do |a|
          if a.respond_to?(:change_level) && a.respond_to?(:max_level)
            a.change_level(a.max_level, false)
          elsif a.respond_to?(:change_exp)
            a.change_exp(99_999_999, false)
          else
            a.exp = 99_999_999
          end
        end
        "Party is at the maximum level"
      else
        NOPE
      end
    end

    def instant_win
      case engine
      when :essentials
        b = @battle
        return NEED_BATTLE unless b
        foes = b.battlers.compact.select { |x| b.pbIsOpposing?(x.index) && !x.fainted? }
        foes.each do |f|
          f.pbReduceHP(f.hp, false)
          f.pbFaint(true)
        end
        (b.party2 || []).each { |p| p.hp = 0 if p }
        b.pbJudge
        "Foes defeated. Choose any command to finish the battle."
      when :xp, :vx, :ace
        return NEED_BATTLE unless in_battle?
        foes = if $game_troop.respond_to?(:enemies) then $game_troop.enemies
               else $game_troop.members end
        foes.each { |e| e.hp = 0 }
        "Enemies defeated. Finish the current turn to win."
      else
        NOPE
      end
    end

    def full_inventory
      case engine
      when :essentials
        return "Bag isn't ready yet. Start or load a game first." unless $PokemonBag
        top = (PBItems.maxValue rescue 0)
        (1..top).each do |i|
          next if (PBItems.getName(i) rescue "").to_s.empty?
          next if (pbIsKeyItem?(i) rescue false)
          have = ($PokemonBag.pbQuantity(i) rescue 0)
          $PokemonBag.pbStoreItem(i, 99 - have) if have < 99
        end
        "Bag filled (key items skipped)"
      when :xp, :vx, :ace
        return "Start or load a game first." unless $game_party
        { 'item' => $data_items, 'weapon' => $data_weapons, 'armor' => $data_armors }.each do |kind, t|
          next unless t
          (1...t.size).each do |i|
            o = t[i]
            next unless o && !o.name.to_s.empty?
            if engine == :xp
              have = $game_party.send("#{kind}_number", i)
              $game_party.send("gain_#{kind}", i, 99 - have) if have < 99
            else
              have = $game_party.item_number(o)
              $game_party.gain_item(o, 99 - have) if have < 99
            end
          end
        end
        "Everything added (99 each)"
      else
        NOPE
      end
    end

    # ---------------------------------------------------------- per frame
    def frame
      return if @errors > 30
      install_hooks
      @tick += 1
      poll if (@tick % 6) == 0
      apply unless @on.empty?
    rescue Exception => e
      @errors += 1
      AnyPlayLog.write("cheats: #{e.class}: #{e.message}") rescue nil
    end

    def poll
      return unless File.exist?(CMD)
      txt = (File.read(CMD) rescue nil)
      File.delete(CMD) rescue nil
      return if txt.nil? || txt.empty?
      txt = txt.dup.force_encoding("UTF-8")
      id, cmd, a, b = txt.chomp.split("\t", 4)
      reply = handle(cmd.to_s, a.to_s, b.to_s)
      File.open(REPLY + ".tmp", "wb") { |f| f.write("#{id}\t#{reply}") }
      File.rename(REPLY + ".tmp", REPLY)
    rescue Exception => e
      AnyPlayLog.write("cheats poll: #{e.class}: #{e.message}") rescue nil
    end

    def apply
      case engine
      when :essentials then apply_ess
      when :xp, :vx, :ace then apply_rpgm
      end
    end

    def apply_ess
      if on?('hpmp') && $Trainer
        pokes.each do |p|
          next if p.egg?
          p.hp = p.totalhp if p.hp != p.totalhp
          p.healPP rescue nil
        end
        if @battle
          @battle.battlers.compact.each do |x|
            next if @battle.pbIsOpposing?(x.index) || x.fainted?
            x.hp = x.totalhp if x.hp != x.totalhp
            (x.moves || []).each { |m| m.pp = m.totalpp if m && m.id > 0 && m.pp < m.totalpp } rescue nil
          end
        end
      end
      if on?('status') && $Trainer
        pokes.each { |p| p.healStatus if !p.egg? && p.status != 0 }
        if @battle
          @battle.battlers.compact.each do |x|
            next if @battle.pbIsOpposing?(x.index) || x.fainted?
            x.pbCureStatus(false) if x.status != 0
          end
        end
      end
      if on?('gold') && $Trainer
        $Trainer.money = 999_999 if $Trainer.money < 999_999
      end
      keep_items if on?('items')
      if on?('fastText') && defined?($PokemonSystem) && $PokemonSystem
        @old_textspeed = $PokemonSystem.textspeed if @old_textspeed.nil?
        $PokemonSystem.textspeed = 2 if $PokemonSystem.textspeed != 2
      end
      common_map_cheats
    end

    def apply_rpgm
      return unless $game_party
      if on?('hpmp') || on?('tp') || on?('status')
        actors.each do |a|
          if on?('hpmp')
            a.hp = maxhp_of(a)
            mp = maxmp_of(a)
            if a.respond_to?(:mp=) then a.mp = mp
            elsif a.respond_to?(:sp=) then a.sp = mp end
          end
          a.tp = a.max_tp if on?('tp') && a.respond_to?(:tp=) && a.respond_to?(:max_tp)
          if on?('status')
            if a.respond_to?(:clear_states)
              a.clear_states
            elsif a.respond_to?(:remove_state) && a.respond_to?(:states)
              a.states.dup.each { |s| a.remove_state(s.respond_to?(:id) ? s.id : s) }
            end
          end
        end
      end
      if on?('gold')
        g = $game_party.instance_variable_get(:@gold).to_i
        $game_party.instance_variable_set(:@gold, 99_999_999) if g < 99_999_999
      end
      keep_items if on?('items')
      common_map_cheats
    end

    def common_map_cheats
      if on?('noclip') && $game_player
        $game_player.through = true
        @through_forced = true
      end
      if on?('noEncounter')
        $game_system.encounter_disabled = true if $game_system && $game_system.respond_to?(:encounter_disabled=)
        if $game_player && $game_player.instance_variable_defined?(:@encounter_count)
          $game_player.instance_variable_set(:@encounter_count, 99_999)
        end
      end
    end

    # Unlimited items: remember the biggest amount seen of every item and
    # put it back whenever the game takes some away.
    def keep_items
      @item_snap ||= {}
      if ess?
        return unless $PokemonBag
        have = {}
        $PokemonBag.pockets.each do |pk|
          next unless pk
          pk.each { |it| have[it[0]] = it[1] if it && it[0].is_a?(Integer) }
        end
        @item_snap.each do |id, q|
          cur = have[id]
          if cur.nil?
            $PokemonBag.pbStoreItem(id, q) rescue nil
          elsif cur < q
            $PokemonBag.pbStoreItem(id, q - cur) rescue nil
          end
        end
        have.each { |id, q| @item_snap[id] = q if q > (@item_snap[id] || 0) }
      else
        h = $game_party.instance_variable_get(:@items)
        return unless h.is_a?(Hash)
        @item_snap.each do |id, q|
          cur = h[id].to_i
          h[id] = q if cur < q
        end
        h.each { |id, q| @item_snap[id] = q if q.to_i > (@item_snap[id] || 0) }
      end
    end

    # ------------------------------------------------------------- hooks
    def install_hooks
      return if @hooked
      return if engine == :unknown
      @hooked = true

      if defined?(Game_Character) && Game_Character.method_defined?(:update)
        Game_Character.class_eval do
          alias_method :__ap_cheat_update, :update
          def update(*args)
            if AnyPlayCheats.speed_on? && $game_player && self.equal?($game_player) && @move_speed
              saved = @move_speed
              @move_speed = saved + AnyPlayCheats.speed_bonus
              begin
                return __ap_cheat_update(*args)
              ensure
                @move_speed = saved
              end
            end
            __ap_cheat_update(*args)
          end
        end
      end

      return unless engine == :essentials

      if defined?(PokeBattle_Move) && PokeBattle_Move.method_defined?(:pbIsCritical?)
        PokeBattle_Move.class_eval do
          alias_method :__ap_cheat_crit, :pbIsCritical?
          def pbIsCritical?(attacker, opponent)
            if AnyPlayCheats.on?('crit') && attacker && !@battle.pbIsOpposing?(attacker.index)
              return true
            end
            __ap_cheat_crit(attacker, opponent)
          end
        end
      end

      if defined?(PokeBattle_Battler)
        PokeBattle_Battler.class_eval do
          if method_defined?(:pbReduceHP)
            alias_method :__ap_cheat_reduce, :pbReduceHP
            def pbReduceHP(amt, anim = false, registerDamage = true)
              foe = @battle.pbIsOpposing?(@index)
              return 0 if !foe && AnyPlayCheats.on?('invincible')
              amt = self.hp if foe && AnyPlayCheats.on?('oneHit') && amt > 0
              __ap_cheat_reduce(amt, anim, registerDamage)
            end
          end
          if method_defined?(:pbReducePP)
            alias_method :__ap_cheat_pp, :pbReducePP
            def pbReducePP(move)
              return true if AnyPlayCheats.on?('freeSkill') && !@battle.pbIsOpposing?(@index)
              __ap_cheat_pp(move)
            end
          end
        end
      end

      if defined?(PokeBattle_Battle)
        PokeBattle_Battle.class_eval do
          if method_defined?(:pbCommandPhase)
            alias_method :__ap_cheat_cmd, :pbCommandPhase
            def pbCommandPhase(*a)
              AnyPlayCheats.battle = self
              __ap_cheat_cmd(*a)
            end
          end
          if method_defined?(:pbEndOfBattle)
            alias_method :__ap_cheat_end, :pbEndOfBattle
            def pbEndOfBattle(*a)
              begin
                __ap_cheat_end(*a)
              ensure
                AnyPlayCheats.battle = nil
              end
            end
          end
          if method_defined?(:pbCanRun?)
            alias_method :__ap_cheat_canrun, :pbCanRun?
            def pbCanRun?(idx)
              return true if AnyPlayCheats.on?('escape') && !@opponent && !@cantescape && !pbIsOpposing?(idx)
              __ap_cheat_canrun(idx)
            end
          end
          if method_defined?(:pbRun)
            alias_method :__ap_cheat_run, :pbRun
            def pbRun(idx, duringBattle = false)
              if AnyPlayCheats.on?('escape') && !@opponent && !@cantescape && !pbIsOpposing?(idx)
                pbSEPlay("Battle flee") rescue nil
                pbDisplayPaused(_INTL("Got away safely!"))
                @decision = 3
                return 1
              end
              __ap_cheat_run(idx, duringBattle)
            end
          end
        end
      end
    end
  end
end

AnyPlayTouch.native_resize = (Graphics.method(:resize_screen) rescue nil)
class << Graphics
  alias_method :__anyplay_update, :update
  def update(*a)
    r = __anyplay_update(*a)
    AnyPlayTouch.frame
    AnyPlayCheats.frame
    r
  end
end
)RB", &preState);
            if (preState)
                rb_set_errinfo(Qnil);
        }
    }
#endif
    
    std::string &customScript = conf.customScript;
    if (!customScript.empty())
        runCustomScript(customScript);
    else
        runRMXPScripts(btData);
    
#if RAPI_FULL > 187
    VALUE exc = rb_errinfo();
#else
    VALUE exc = rb_gv_get("$!");
#endif
    if (!NIL_P(exc) && !rb_obj_is_kind_of(exc, rb_eSystemExit))
        showExc(exc, btData);
    
    ruby_cleanup(0);
    
    shState->rtData().rqTermAck.set();
}

static void mriBindingTerminate() { rb_raise(rb_eSystemExit, " "); }

static void mriBindingReset() { rb_raise(getRbData()->exc[Reset], " "); }
