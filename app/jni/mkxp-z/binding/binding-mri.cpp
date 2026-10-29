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
                    compatFix = rb_eval_string(R"RB(
lambda { |s|
  begin
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
)RB", &reqState);
        if (reqState)
            rb_set_errinfo(Qnil);

        // AnyPlay: safe dispose, full-screen fit, tap-to-walk (see anyplay_prelude.rb)
        {
            int preState = 0;
            rb_eval_string_protect(R"RB(# AnyPlay RGSS plugin: runs before the game's own scripts (Android only).
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
  @was_down = false
  @down_frames = 0
  @sx = @sy = @lx = @ly = 0
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
            return true
          end
          __ap_trigger(b)
        end
        if Input.respond_to?(:repeat?)
          alias_method :__ap_repeat, :repeat?
          def repeat?(b)
            if AnyPlayTouch.press_c > 0 && (b == Input::C)
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
        @press_c = 1          # dialogs, menus, title: tap = confirm
        return
      end
      tx, ty = screen_to_tile(mx, my)
      if tx == $game_player.x && ty == $game_player.y
        @press_c = 1
        return
      end
      route, face = plan(tx, ty)
      AnyPlayLog.write("  target=#{tx},#{ty} route=#{route.length} face=#{face.inspect}")
      if route.empty?
        if face
          turn(face); @press_c = 1
        elsif (ev = event_at(tx, ty))
          @press_c = 1
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
          turn(@face); @face = nil; @press_c = 1
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
      down = mouse_down?
      if down
        @lx = (Input.mouse_x rescue @lx)
        @ly = (Input.mouse_y rescue @ly)
        if !@was_down
          @down_frames = 0; @sx = @lx; @sy = @ly
        end
        @down_frames += 1
      elsif @was_down
        if @down_frames < 25 && (@lx - @sx).abs < 24 && (@ly - @sy).abs < 24
          on_tap(@lx, @ly)
        end
      end
      @was_down = down
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

AnyPlayTouch.native_resize = (Graphics.method(:resize_screen) rescue nil)
class << Graphics
  alias_method :__anyplay_update, :update
  def update(*a)
    r = __anyplay_update(*a)
    AnyPlayTouch.frame
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
