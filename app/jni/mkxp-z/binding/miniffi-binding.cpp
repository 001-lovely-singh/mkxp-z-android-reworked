// Most of the MiniFFI class was taken from Ruby 1.8's Win32API.c,
// it's just as basic but should work fine for the moment

#include <string>
#include <cctype>
#include <SDL.h>
#include <cstdint>

#include "filesystem/filesystem.h"
#include "miniffi.h"
#include "binding-util.h"

#if RAPI_MAJOR >= 2
#include <ruby/thread.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#define MVAL2RB(v) ULONG2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2ULONG(v)
#else
#ifdef __MINGW64__
#define MVAL2RB(v) ULL2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2ULL(v)
#else
#define MVAL2RB(v) UINT2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2UINT(v)
#endif
#endif

#define _T_VOID 0
#define _T_NUMBER 1
#define _T_POINTER 2
#define _T_INTEGER 3
#define _T_BOOL 4

#if RAPI_FULL > 187
DEF_TYPE_CUSTOMFREE(MiniFFI, SDL_UnloadObject);
#else
DEF_ALLOCFUNC_CUSTOMFREE(MiniFFI, SDL_UnloadObject);
#endif

static void *MiniFFI_GetFunctionHandle(void *libhandle, const char *func) {
    if (!libhandle)
        return 0;
    return SDL_LoadFunction(libhandle, func);
}

// MiniFFI.new(library, function[, imports[, exports]])
// Yields itself in blocks

RB_METHOD(MiniFFI_initialize) {
    VALUE libname, func, imports, exports;
    rb_scan_args(argc, argv, "22", &libname, &func, &imports, &exports);
    SafeStringValue(libname);
    SafeStringValue(func);
#ifdef __APPLE__
    void *hlib = SDL_LoadObject(mkxp_fs::normalizePath(RSTRING_PTR(libname), 1, 1).c_str());
#else
    void *hlib = SDL_LoadObject(RSTRING_PTR(libname));
#endif
    setPrivateData(self, hlib);
    void *hfunc = MiniFFI_GetFunctionHandle(hlib, RSTRING_PTR(func));
#ifdef __WIN32__
    if (hlib && !hfunc) {
        VALUE func_a = rb_str_new3(func);
        func_a = rb_str_cat(func_a, "A", 1);
        hfunc = SDL_LoadFunction(hlib, RSTRING_PTR(func_a));
    }
#endif
#ifdef __ANDROID__
    // AnyPlay: Windows games ask for kernel32/user32/etc. which don't exist on
    // Android. Instead of raising (which crashes the game on startup), keep a
    // "dummy" function: calling it does nothing and returns 0 / "".
    if (!hfunc) {
        rb_iv_set(self, "_dummy", Qtrue);
        hfunc = 0;
    }
#else
    if (!hfunc)
        rb_raise(rb_eRuntimeError, "%s", SDL_GetError());
#endif
    
    rb_iv_set(self, "_func", MVAL2RB((mffi_value)hfunc));
    rb_iv_set(self, "_funcname", func);
    rb_iv_set(self, "_libname", libname);
    
    VALUE ary_imports = rb_ary_new();
    VALUE *entry;
    switch (TYPE(imports)) {
        case T_NIL:
            break;
        case T_ARRAY:
            entry = RARRAY_PTR(imports);
            for (int i = 0; i < RARRAY_LEN(imports); i++) {
                SafeStringValue(entry[i]);
                switch (*(char *)RSTRING_PTR(entry[i])) {
                    case 'N':
                    case 'n':
                    case 'L':
                    case 'l':
                        rb_ary_push(ary_imports, INT2FIX(_T_NUMBER));
                        break;
                        
                    case 'P':
                    case 'p':
                        rb_ary_push(ary_imports, INT2FIX(_T_POINTER));
                        break;
                        
                    case 'I':
                    case 'i':
                        rb_ary_push(ary_imports, INT2FIX(_T_INTEGER));
                        break;
                        
                    case 'B':
                    case 'b':
                        rb_ary_push(ary_imports, INT2FIX(_T_BOOL));
                        break;
                }
            }
            break;
        default:
            SafeStringValue(imports);
            const char *s = RSTRING_PTR(imports);
            for (int i = 0; i < RSTRING_LEN(imports); i++) {
                switch (*s++) {
                    case 'N':
                    case 'n':
                    case 'L':
                    case 'l':
                        rb_ary_push(ary_imports, INT2FIX(_T_NUMBER));
                        break;
                        
                    case 'P':
                    case 'p':
                        rb_ary_push(ary_imports, INT2FIX(_T_POINTER));
                        break;
                        
                    case 'I':
                    case 'i':
                        rb_ary_push(ary_imports, INT2FIX(_T_INTEGER));
                        break;
                        
                    case 'B':
                    case 'b':
                        rb_ary_push(ary_imports, INT2FIX(_T_BOOL));
                        break;
                }
            }
            break;
    }
    
    if (MINIFFI_MAX_ARGS < RARRAY_LEN(ary_imports))
        rb_raise(rb_eRuntimeError, "too many parameters: %ld/%ld\n",
                 RARRAY_LEN(ary_imports), MINIFFI_MAX_ARGS);
    
    rb_iv_set(self, "_imports", ary_imports);
    int ex;
    if (NIL_P(exports)) {
        ex = _T_VOID;
    } else {
        SafeStringValue(exports);
        switch (*RSTRING_PTR(exports)) {
            case 'V':
            case 'v':
                ex = _T_VOID;
                break;
                
            case 'N':
            case 'n':
            case 'L':
            case 'l':
                ex = _T_NUMBER;
                break;
                
            case 'P':
            case 'p':
                ex = _T_POINTER;
                break;
                
            case 'I':
            case 'i':
                ex = _T_INTEGER;
                break;
                
            case 'B':
            case 'b':
                ex = _T_BOOL;
                break;
        }
    }
    rb_iv_set(self, "_exports", INT2FIX(ex));
    if (rb_block_given_p())
        rb_yield(self);
    return Qnil;
}

#if RAPI_MAJOR >= 2
typedef struct {
    MINIFFI_FUNC function;
    MiniFFIFuncArgs *args;
    int nparams;
} MFFICallCBArgs;

void* miniffi_call_cb(void *args) {
    MFFICallCBArgs *a = (MFFICallCBArgs*)args;
    return (void*)miniffi_call_intern(a->function, a->args, a->nparams);
    }
#endif

#ifdef __ANDROID__
// AnyPlay: map a Windows virtual-key code to an SDL scancode (0 = unknown).
static int anyplayVkToScancode(int vk) {
    if (vk >= 0x41 && vk <= 0x5A) return SDL_SCANCODE_A + (vk - 0x41);
    if (vk >= 0x31 && vk <= 0x39) return SDL_SCANCODE_1 + (vk - 0x31);
    if (vk == 0x30) return SDL_SCANCODE_0;
    if (vk >= 0x61 && vk <= 0x69) return SDL_SCANCODE_KP_1 + (vk - 0x61);
    if (vk == 0x60) return SDL_SCANCODE_KP_0;
    if (vk >= 0x70 && vk <= 0x7B) return SDL_SCANCODE_F1 + (vk - 0x70);
    switch (vk) {
        case 0x08: return SDL_SCANCODE_BACKSPACE;
        case 0x09: return SDL_SCANCODE_TAB;
        case 0x0D: return SDL_SCANCODE_RETURN;
        case 0x10: case 0xA0: return SDL_SCANCODE_LSHIFT;
        case 0xA1: return SDL_SCANCODE_RSHIFT;
        case 0x11: case 0xA2: return SDL_SCANCODE_LCTRL;
        case 0xA3: return SDL_SCANCODE_RCTRL;
        case 0x12: case 0xA4: return SDL_SCANCODE_LALT;
        case 0xA5: return SDL_SCANCODE_RALT;
        case 0x1B: return SDL_SCANCODE_ESCAPE;
        case 0x20: return SDL_SCANCODE_SPACE;
        case 0x21: return SDL_SCANCODE_PAGEUP;
        case 0x22: return SDL_SCANCODE_PAGEDOWN;
        case 0x23: return SDL_SCANCODE_END;
        case 0x24: return SDL_SCANCODE_HOME;
        case 0x25: return SDL_SCANCODE_LEFT;
        case 0x26: return SDL_SCANCODE_UP;
        case 0x27: return SDL_SCANCODE_RIGHT;
        case 0x28: return SDL_SCANCODE_DOWN;
        case 0x2D: return SDL_SCANCODE_INSERT;
        case 0x2E: return SDL_SCANCODE_DELETE;
        case 0xBA: return SDL_SCANCODE_SEMICOLON;
        case 0xBB: return SDL_SCANCODE_EQUALS;
        case 0xBC: return SDL_SCANCODE_COMMA;
        case 0xBD: return SDL_SCANCODE_MINUS;
        case 0xBE: return SDL_SCANCODE_PERIOD;
        case 0xBF: return SDL_SCANCODE_SLASH;
    }
    return 0;
}

// AnyPlay: is this Windows virtual key currently held? (keyboard or touch)
static bool anyplayVkDown(int vk) {
    if (vk == 0x01 || vk == 0x02) {            // mouse buttons (touch = left click)
        Uint32 b = SDL_GetMouseState(NULL, NULL);
        return (b & (vk == 0x01 ? SDL_BUTTON_LMASK : SDL_BUTTON_RMASK)) != 0;
    }
    int sc = anyplayVkToScancode(vk);
    if (!sc) return false;
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    if (ks[sc]) return true;
    // generic SHIFT / CTRL / ALT: either side counts
    if (vk == 0x10) return ks[SDL_SCANCODE_RSHIFT] != 0;
    if (vk == 0x11) return ks[SDL_SCANCODE_RCTRL] != 0;
    if (vk == 0x12) return ks[SDL_SCANCODE_RALT] != 0;
    return false;
}
#endif

RB_METHOD(MiniFFI_call) {
    MiniFFIFuncArgs param;
#define params param.params
    VALUE func = rb_iv_get(self, "_func");
    VALUE own_imports = rb_iv_get(self, "_imports");
    VALUE own_exports = rb_iv_get(self, "_exports");
    MINIFFI_FUNC ApiFunction = (MINIFFI_FUNC)RB2MVAL(func);
    VALUE args;
    int items = rb_scan_args(argc, argv, "0*", &args);
    int nimport = RARRAY_LEN(own_imports);
    if (items != nimport)
        rb_raise(rb_eRuntimeError,
                 "wrong number of parameters: expected %d, got %d", nimport, items);
    
#ifdef __ANDROID__
    // AnyPlay: dummy Windows function -> ignore the call safely.
    if (RTEST(rb_iv_get(self, "_dummy"))) {
        // Emulate the few Windows functions games use for INPUT and for
        // "is my window focused?" checks. Without this every key looked
        // "not pressed" and the game ignored all controls.
        {
            VALUE fn = rb_iv_get(self, "_funcname");
            std::string f = NIL_P(fn) ? "" : RSTRING_PTR(fn);
            for (auto &c : f) c = (char)tolower((unsigned char)c);
            if (f == "getasynckeystate" || f == "getkeystate") {
                int vk = items > 0 ? NUM2INT(rb_ary_entry(args, 0)) : 0;
                return MVAL2RB(anyplayVkDown(vk) ? 0x8000 : 0);
            }
            if (f == "getforegroundwindow" || f == "getactivewindow" ||
                f == "getfocus" || f == "findwindow" || f == "findwindowex" ||
                f == "findwindowa" || f == "findwindowexa" ||
                f == "getwindowthreadprocessid" || f == "getcurrentthreadid" ||
                f == "getcurrentprocessid")
                return MVAL2RB(1);
        }
        switch (FIX2INT(own_exports)) {
            case _T_POINTER:
                return rb_utf8_str_new_cstr("");
            case _T_BOOL:
                return Qfalse;
            default: {
                // Internet functions (wininet/winhttp) return a "handle"; 0 means
                // "failed". Some games stop with an error on 0, so for those
                // libraries pretend the call worked (downloads then simply
                // come back empty). Everything else returns 0.
                VALUE lib = rb_iv_get(self, "_libname");
                if (!NIL_P(lib)) {
                    std::string l = RSTRING_PTR(lib);
                    for (auto &c : l) c = (char)tolower((unsigned char)c);
                    if (l.find("wininet") != std::string::npos ||
                        l.find("winhttp") != std::string::npos)
                        return MVAL2RB(1);
                }
                return MVAL2RB(0);
            }
        }
    }
#endif

    for (int i = 0; i < nimport; i++) {
        VALUE str = rb_ary_entry(args, i);
        mffi_value lParam = 0;
        switch (FIX2INT(rb_ary_entry(own_imports, i))) {
            case _T_POINTER:
                if (NIL_P(str)) {
                    lParam = 0;
                } else if (FIXNUM_P(str)) {
                    lParam = RB2MVAL(str);
                } else {
                    StringValue(str);
                    rb_str_modify(str);
                    lParam = (mffi_value)RSTRING_PTR(str);
                }
                break;
                
            case _T_BOOL:
                rb_bool_arg(rb_ary_entry(args, i), (bool*)&lParam);
                break;
                
            case _T_INTEGER:
#if INTPTR_MAX == INT64_MAX
                lParam = RB2MVAL(rb_ary_entry(args, i)) & UINT32_MAX;
                break;
#endif
            case _T_NUMBER:
            default:
                lParam = RB2MVAL(rb_ary_entry(args, i));
                break;
        }
        params[i] = lParam;
    }
#if RAPI_MAJOR >= 2
    MFFICallCBArgs cb_args {ApiFunction, &param, nimport};
    mffi_value ret = (mffi_value)rb_thread_call_without_gvl(miniffi_call_cb, &cb_args, 0, 0);
#else
    mffi_value ret = miniffi_call_intern(ApiFunction, &param, nimport);
#endif
    
    switch (FIX2INT(own_exports)) {
        case _T_NUMBER:
        case _T_INTEGER:
            return MVAL2RB(ret);
            
        case _T_POINTER:
            return rb_utf8_str_new_cstr((char *)ret);
            
        case _T_BOOL:
            return rb_bool_new(ret);
            
        case _T_VOID:
        default:
            return MVAL2RB(0);
    }
}

void MiniFFIBindingInit() {
    VALUE cMiniFFI = rb_define_class("MiniFFI", rb_cObject);
#if RAPI_FULL > 187
    rb_define_alloc_func(cMiniFFI, classAllocate<&MiniFFIType>);
#else
    rb_define_alloc_func(cMiniFFI, MiniFFIAllocate);
#endif
    _rb_define_method(cMiniFFI, "initialize", MiniFFI_initialize);
    _rb_define_method(cMiniFFI, "call", MiniFFI_call);
    rb_define_alias(cMiniFFI, "Call", "call");
    
    rb_define_const(rb_cObject, "Win32API", cMiniFFI);
}
