#pragma once

#include "def.h"
#include "mem.h"
#include "mls.h"
#include "stl.h"

namespace flag {

// add alias for a flag, @new_name must have static life cycle
fastring alias(const char* flag_name, const char* new_name);

// set default path of the config file
void set_config_path(const char* path);

// set program version, @ver must have static life cycle
void set_version(const char* ver);

// flag attributes 
enum _attr_t {
    attr_default = 'd',      // support both command-line and config file
    attr_command_line = 'c', // support command-line only
    attr_hidden = 'h',       // hidden, support neither
};

// set attr of a flag
fastring set_attr(const char* flag_name, _attr_t a);

// set value of a flag
fastring set_value(const char* flag_name, const fastring& value);

// add a callback to be called after command line args are parsed
void run_after_parse(void(*cb)());

// add a callback to be called before command line args are parsed
void run_before_parse(void(*cb)());

// Parse both command line args and config file by default, 
// parse command line args only if @command_line_only is true.
co::vector<fastring> parse(int argc, char** argv, bool command_line_only=false);

namespace xx {

void add_flag(
    char type, const char* name, const char* value, const char* help, 
    const char* file, int line, void* addr, const char* alias
);

void add_flag(
    char type, const char* name, const char* value, const co::mls& help, 
    const char* file, int line, void* addr, const char* alias
);

} // xx
} // flag

#define _CO_DEC_FLAG(type, name) extern type FLG_##name;

#define DEC_bool(name)    _CO_DEC_FLAG(bool, name)
#define DEC_int32(name)   _CO_DEC_FLAG(int32, name)
#define DEC_int64(name)   _CO_DEC_FLAG(int64, name)
#define DEC_uint32(name)  _CO_DEC_FLAG(uint32, name)
#define DEC_uint64(name)  _CO_DEC_FLAG(uint64, name)
#define DEC_double(name)  _CO_DEC_FLAG(double, name)
#define DEC_string(name)  extern fastring& FLG_##name

#define _CO_DEF_FLAG(type, iden, name, value, help, ...) \
    type FLG_##name = []() { \
        ::flag::xx::add_flag( \
            iden, #name, #value, help, __FILE__, __LINE__, &FLG_##name, ""#__VA_ARGS__ \
        ); \
        return value; \
    }();

#define DEF_bool(name, value, help, ...)   _CO_DEF_FLAG(bool,   'b', name, value, help, __VA_ARGS__)
#define DEF_int32(name, value, help, ...)  _CO_DEF_FLAG(int32,  'i', name, value, help, __VA_ARGS__)
#define DEF_int64(name, value, help, ...)  _CO_DEF_FLAG(int64,  'I', name, value, help, __VA_ARGS__)
#define DEF_uint32(name, value, help, ...) _CO_DEF_FLAG(uint32, 'u', name, value, help, __VA_ARGS__)
#define DEF_uint64(name, value, help, ...) _CO_DEF_FLAG(uint64, 'U', name, value, help, __VA_ARGS__)
#define DEF_double(name, value, help, ...) _CO_DEF_FLAG(double, 'd', name, value, help, __VA_ARGS__)

#define DEF_string(name, value, help, ...) \
    fastring& FLG_##name = *[]() { \
        auto _##name = ::co::_make_static<fastring>(value); \
        ::flag::xx::add_flag('s', #name, #value, help, __FILE__, __LINE__, _##name, ""#__VA_ARGS__); \
        return _##name; \
    }();
