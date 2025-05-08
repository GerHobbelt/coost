#include "co/flag.h"
#include "co/cout.h"
#include "co/defer.h"
#include "co/fs.h"
#include "co/stl.h"
#include "co/str.h"
#include "co/thread.h"

DEF_mlstr(help, "@c 显示帮助信息", "@c show help info");
DEF_mlstr(version, "@c 显示版本信息", "@c show version");
DEF_mlstr(mkconf, "@c 生成配置文件", "@c generate config file");
DEF_mlstr(e_range, "超出数值范围", "out of range");
DEF_mlstr(e_inval, "无效数值", "invalid value");
DEF_mlstr(e_not_found, "未找到flag", "flag not found");
DEF_mlstr(e_name_used, "已用于", "already used in");
DEF_mlstr(e_redef, "重定义于", "redefined in");
DEF_mlstr(e_multi_alias, "不允许多个别名", "multiple aliases are not allowed");
DEF_mlstr(e_alias_conflict, "别名冲突", "alias name conflict");
DEF_mlstr(e_no_value, "值未设置", "value not set");
DEF_mlstr(e_open_failed, "打开文件失败", "open file failed");
DEF_mlstr(e_conf, "无效配置", "invalid config");
DEF_mlstr(e_quote, "引号缺失", "quote missing");
DEF_mlstr(e_badstr, "无效字符串值", "invalid string value");

DEF_bool(help, false, MLS_help);
DEF_bool(version, false, MLS_version);
DEF_bool(mkconf, false, MLS_mkconf);

static bool g_command_line_only = false;

namespace flag {
namespace xx {

struct Flag;
struct Mod {
    Mod() = default;
    ~Mod() = default;

    typedef void(*parse_cb_t)();

    void add_flag(Flag* f);
    Flag* find_flag(const char* name);

    fastring alias(const char* name, const char* new_name);
    fastring set_flag_attr(const char* name, _attr_t a);
    fastring set_flag_value(const char* name, const fastring& value);
    fastring set_bool_flags(const char* name);

    void set_config_path(const char* path) { _config_path = path; }
    void set_version(const char* ver) { _version = ver; }
    void add_parse_cb(parse_cb_t cb, char c) { _cbs[c != 'a'].push_back(cb); }
    void run_parse_cb(char c) {
        auto& cbs = _cbs[c != 'a'];
        if (!cbs.empty()) {
            for (auto& cb : cbs) cb();
            co::vector<parse_cb_t>().swap(cbs);
        }
    }

    void print_help(const fastring& exe);
    void make_config(const fastring& exe);
    void parse_config(const fastring& config);
    co::vector<fastring> parse_commandline(int argc, char** argv);

    co::vector<fastring> analyze_args(
        const co::vector<fastring>& args, co::map<fastring, fastring>& kv,
        co::vector<fastring>& bools
    );

    co::map<const char*, Flag*> _flags;
    co::vector<parse_cb_t> _cbs[2];
    fastring _config_path;
    fastring _version;
};

static Mod* g_mod;

inline Mod& mod() {
    return g_mod ? *g_mod : *(g_mod = co::_make_static<Mod>());
}

struct Flag {
    const char* get_help() const;
    const char* set_value(const fastring& v);
    fastring get_value() const;
    void print(size_t m, size_t n) const;

    char iden;
    char attr;
    bool mls;  // multi-language support
    const char* name;
    const char* alias;
    const char* value; // default value
    union {
        const char* help;
        const co::mlstr* mlp;
    };
    const char* file;
    int line;
    void* addr;
};

const char* Flag::get_help() const {
    const char* h = !mls ? help : mlp->value();
    if (*h == '@') {
        const char c = *(h + 1);
        if (c == 'd' || c == 'c' || c == 'h') {
            h += 2;
            while (*h && *h == ' ') ++h;
        }
    }
    return h;
}

const char* Flag::set_value(const fastring& v) {
    int err = 0;
    const char* s = v.c_str();
    switch (this->iden) {
    case 's':
        *static_cast<fastring*>(this->addr) = v;
        break;
    case 'b':
        *static_cast<bool*>(this->addr) = str::to_bool(s, &err);
        break;
    case 'i':
        *static_cast<int32*>(this->addr) = str::to_int32(s, &err);
        break;
    case 'u':
        *static_cast<uint32*>(this->addr) = str::to_uint32(s, &err);
        break;
    case 'I':
        *static_cast<int64*>(this->addr) = str::to_int64(s, &err);
        break;
    case 'U':
        *static_cast<uint64*>(this->addr) = str::to_uint64(s, &err);
        break;
    case 'd':
        *static_cast<double*>(this->addr) = str::to_double(s, &err);
        break;
    }

    switch (err) {
    case 0:
        return "";
    case ERANGE:
        return MLS_e_range.value();
    default:
        return MLS_e_inval.value();
    }
}

template<typename T>
fastring int2str(T t) {
    int i = -1;
    if (t > 8192 || (t < 0 && t < -8192)) {
        while (t != 0 && (t & 1023) == 0) {
            t >>= 10;
            if (++i == 4) break;
        }
    }
    fastring s = str::from(t);
    if (i >= 0) s.append("kmgtp"[i]);
    return s;
}

fastring Flag::get_value() const {
    switch (this->iden) {
    case 's':
        return *static_cast<fastring*>(this->addr);
    case 'b':
        return str::from(*static_cast<bool*>(this->addr));
    case 'i':
        return int2str(*static_cast<int32*>(this->addr));
    case 'u':
        return int2str(*static_cast<uint32*>(this->addr));
    case 'I':
        return int2str(*static_cast<int64*>(this->addr));
    case 'U':
        return int2str(*static_cast<uint64*>(this->addr));
    case 'd':
        return str::from(*static_cast<double*>(this->addr));
    default:
        return fastring();
    }
}

void Flag::print(size_t m, size_t n) const {
    if (attr == flag::attr_hidden) return;

    auto& f = *this;
    co::cout(color::bold, color::green, "  -", f.name);
    if (*f.alias) co::cout(',', f.alias);
    co::cout(color::deflt).flush();

    const char* h = f.get_help();
    if (n < m) co::cout(fastring(m - n, ' '));
    if (n <= m) {
        co::cout(
            color::bold, color::yellow, "  ", f.iden, "  ",
            color::deflt, h,
            color::bold, color::blue, "  (", f.get_value(), ')', color::deflt, '\n'
        );
    } else {
        co::cout(
            '\n', fastring(m, ' '),
            color::bold, color::yellow, "  ", f.iden, "  ",
            color::deflt, h,
            color::bold, color::blue, "  (", f.get_value(), ')', color::deflt, '\n'
        );
    }
}

void Mod::add_flag(Flag* f) {
    auto r = _flags.emplace(f->name, f);
    if (!r.second) {
        auto& g = r.first->second;
        co::cout(
            "flag ", f->name, ' ', MLS_e_redef, ": ", 
            g->file, ':', g->line, ", ", f->file, ':', f->line, co::endl
        );
        ::exit(0);
    }

    const char* const a = f->alias;
    if (*a) {
        if (strchr(a, ',') != NULL) {
            co::cout(MLS_e_multi_alias, ", ", f->file, ':', f->line, co::endl);
            ::exit(0);
        }
        auto r = _flags.emplace(a, f);
        if (!r.second) {
            auto& g = r.first->second;
            co::cout(
                MLS_e_alias_conflict, ": ", f->file, ':', f->line, ", ",
                g->file, ':', g->line, co::endl
            );
            ::exit(0);
        }
    }
}

inline Flag* Mod::find_flag(const char* name) {
    auto it = _flags.find(name);
    return it != _flags.end() ? it->second : NULL;
}

fastring Mod::alias(const char* name, const char* new_name) {
    fastring e;
    auto f = this->find_flag(name);
    if (!f) {
        e.cat(MLS_e_not_found, ": ", name);
        return e;
    }

    if (!new_name || !*new_name) {
        if (*f->alias) {
            _flags.erase(f->alias);
            f->alias = "";
        }
        return e;
    }

    if (strcmp(f->alias, new_name) == 0) return e;

    auto r = _flags.emplace(new_name, f);
    if (!r.second) {
        auto& g = r.first->second;
        e.cat(
            new_name, ' ', MLS_e_name_used, " flag ", g->name,
            '(', g->file, ':', g->line, ')'
        );
        return e;
    }

    if (*f->alias) _flags.erase(f->alias);
    f->alias = new_name;
    return e;
}

fastring Mod::set_flag_attr(const char* name, _attr_t a) {
    fastring e;
    Flag* f = this->find_flag(name);
    if (f) {
        f->attr = a;
    } else {
        e.cat(MLS_e_not_found, ": ", name);
    }
    return e;
}

fastring Mod::set_flag_value(const char* name, const fastring& value) {
    fastring e;
    Flag* f = this->find_flag(name);
    if (f) {
        const char* s = f->set_value(value);
        if (*s) e.cat(s, ": ", value);
    } else {
        e.cat(MLS_e_not_found, ": ", name);
    }
    return e;
}

// set_bool_flags("abc"):  -abc -> true  or  -a, -b, -c -> true
fastring Mod::set_bool_flags(const char* name) {
    fastring e;
    Flag* f = this->find_flag(name);
    if (f) {
        if (f->iden == 'b') {
            *static_cast<bool*>(f->addr) = true;
        } else {
            e.cat("flag ", name, ", ", MLS_e_no_value);
        }
        return e;
    }

    const size_t n = strlen(name);
    if (n == 1) {
        e.cat(MLS_e_not_found, ": ", name);
        return e;
    }

    char sub[2] = { 0 };
    for (size_t i = 0; i < n; ++i) {
        sub[0] = name[i];
        f = this->find_flag(sub);
        if (f && f->iden == 'b') {
            *static_cast<bool*>(f->addr) = true;
            continue;
        }
        e.cat(MLS_e_not_found, ": ", name);
        return e;
    }

    return e;
}

void Mod::print_help(const fastring& exe) {
    co::cout(color::bold, "usage:  ", color::cyan, exe);
    if (!g_command_line_only) co::cout(" [", exe, ".conf]");
    co::cout(" [flag [value]]...\n\n", color::deflt);

    size_t m = 0;
    co::vector<size_t> len;
    len.reserve(_flags.size());

    Flag* ff[3];
    size_t nn[3];
    for (auto it = _flags.begin(); it != _flags.end(); ++it) {
        auto& f = *(it->second);
        size_t n = strlen(f.name) + 3;
        if (*f.alias) n += strlen(f.alias) + 1;
        len.push_back(n);
        if (n <= 21 && m < n) m = n;

        if (f.addr == &FLG_help) {
            ff[0] = &f;
            nn[0] = n;
        } else if (f.addr == &FLG_version) {
            ff[1] = &f;
            nn[1] = n;
        } else if (f.addr == &FLG_mkconf) {
            ff[2] = &f;
            nn[2] = n;
        }
    }

    co::cout(
        color::bold, "flags:  -name[,alias]  type  comments  (default value)\n",
        color::deflt
    );

    ff[0]->print(m, nn[0]);
    ff[1]->print(m, nn[1]);
    if (!g_command_line_only) ff[2]->print(m, nn[2]);
    co::cout(co::endl);

    int i = 0;
    for (auto it = _flags.begin(); it != _flags.end(); ++it) {
        const size_t n = len[i++];
        auto& f = *(it->second);
        if (&f == ff[0] || &f == ff[1] || &f == ff[2]) continue;
        if (*f.alias && strcmp(it->first, f.name) != 0) continue;
        f.print(m, n);
    }
    co::cout().flush();
}

// add quotes to string if necessary
inline void format_str(fastring& s) {
    if (s.find_first_of("\"'`#") != s.npos) {
        fastring r(std::move(s));
        if (!r.contains('"')) {
            s << '"' << r << '"';
        } else if (!r.contains('\'')) {
            s << '\'' << r << '\'';
        } else {
            s << "```" << r << "```";
        }
    }
}

void Mod::make_config(const fastring& exe) {
    co::map<const char*, co::map<int, Flag*>> o;
    for (auto it = _flags.begin(); it != _flags.end(); ++it) {
        auto& f = *it->second;
        if (f.attr == flag::attr_default) o[f.file][f.line] = &f;
    }

    fastring fname(exe);
    fname.remove_suffix(".exe");
    fname += ".conf";

    fs::fstream f(fname.c_str(), 'w');
    if (!f) {
        co::cout(MLS_e_open_failed, ": ", fname, co::endl);
        return;
    }

    const int COMMENT_LINE_LEN = 72;
    f << fastring(COMMENT_LINE_LEN, '#') << '\n'
      << "###  > # for comments\n"
      << "###  > k,m,g,t,p (8k for 8192, etc.)\n"
      << fastring(COMMENT_LINE_LEN, '#') << "\n\n\n";

    for (auto it = o.begin(); it != o.end(); ++it) {
        f << "#" << fastring(COMMENT_LINE_LEN - 1, '=') << '\n';
        const auto& x = it->second;
        for (auto kt = x.begin(); kt != x.end(); ++kt) {
                auto& flag = *(kt->second);
                fastring v = flag.get_value();
                if (flag.iden == 's') v.escape();
                auto h = flag.get_help();
                f << "# " << str::replace(h, "\n", "\n# ") << '\n'
                  << flag.name << " = ";
                if (!v.contains('\\')) {
                    f << v << "\n\n";
                } else {
                    f << '"' << v << '"' << "\n\n";
                }
        }
        f << '\n';
    }

    f.flush();
}

// @kv:  for -key value
// @k:   for -a, -xyz
// return non-flag elements (etc. hello, -8, -8k, -, --, --- ...)
co::vector<fastring> Mod::analyze_args(
    const co::vector<fastring>& args, co::map<fastring, fastring>& kv, co::vector<fastring>& k 
) {
    co::vector<fastring> res;

    for (size_t i = 0; i < args.size(); ++i) {
        const fastring& arg = args[i];
        const size_t p = arg.find_first_not_of('-');
        if (p == 0 || p == arg.npos || (p == 1 && '0' <= arg[1] && arg[1] <= '9')) {
            res.push_back(arg);
            continue;
        }

        // flag: -a, -a b, or -j4
        {
            Flag* f = 0;
            fastring next;
            fastring name = arg.substr(p);

            // for -j4
            if (name.size() > 1 && (('0' <= name[1] && name[1] <= '9') || name[1] == '-')) {
                char sub[2] = { name[0], '\0' };
                if (!find_flag(name.c_str()) && find_flag(sub)) {
                    kv[name.substr(0, 1)] = name.substr(1);
                    continue;
                }
            }

            if (i + 1 == args.size()) goto no_value;

            next = args[i + 1];
            if (next.starts_with('-') && next.find_first_not_of('-') != next.npos) {
                if (next[1] < '0' || next[1] > '9') goto no_value;
            }

            f = find_flag(name.c_str());
            if (!f) goto no_value;
            if (f->iden != 'b') goto has_value;
            if (next == "0" || next == "1" || next == "false" || next == "true") goto has_value;

        no_value:
            k.push_back(name);
            continue;

        has_value:
            kv[name] = next;
            ++i;
            continue;
        };
    }

    return res;
}

inline fastring _exename(const char* path) {
    const char* x = strrchr(path, '/');
    if (!x) x = strrchr(path, '\\');
    return x ? fastring(x + 1) : fastring(path);
}

co::vector<fastring> Mod::parse_commandline(int argc, char** argv) {
    defer(
        this->_version.reset();
        this->_config_path.reset();
    );

    if (argc <= 1) return co::vector<fastring>();

    co::vector<fastring> args;
    args.reserve(argc - 1);
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    fastring exe = _exename(argv[0]);
    exe.remove_suffix(".exe");

    co::map<fastring, fastring> kv;
    co::vector<fastring> k;
    co::vector<fastring> v = this->analyze_args(args, kv, k);

    if (v.empty() && kv.empty() && k.size() == 1) {
        const auto& name = k[0];
        auto f = this->find_flag(name.c_str());
        if (f) {
            if (strcmp(f->name, "help") == 0) {
                this->print_help(exe);
                ::exit(0);
            }
            if (strcmp(f->name, "version") == 0) {
                co::cout(_version, co::endl);
                ::exit(0);
            }
        }
    }

    if (!g_command_line_only) {
        if (!v.empty() && v[0].ends_with(".conf")) _config_path = v[0];
        if (!_config_path.empty()) this->parse_config(_config_path);
    }

    for (auto it = kv.begin(); it != kv.end(); ++it) {
        fastring e = this->set_flag_value(it->first.c_str(), it->second);
        if (!e.empty()) {
            co::cout(e, co::endl);
            ::exit(0);
        }
    }

    for (size_t i = 0; i < k.size(); ++i) {
        fastring e = this->set_bool_flags(k[i].c_str());
        if (!e.empty()) {
            co::cout(e, co::endl);
            ::exit(0);
        }
    }

    return v;
}

const char* remove_quotes_and_comments(fastring& s) {
    if (s.empty()) return "";

    size_t p;
    const char c = s[0];

    if (c == '"' || c == '\'') {
        p = s.rfind(c);
        if (p == 0) return MLS_e_quote.value();

        p = s.find_first_not_of(" \t", p + 1);
        if (p == s.npos) {
            s.trim(" \t", 'r');
        } else if (s[p] == '#') {
            s.resize(p);
            s.trim(" \t", 'r');
        } else {
            return MLS_e_badstr.value();
        }

        s.trim(1, 'b');
        return "";
    }

    p = s.find('#');
    if (p != s.npos) {
        s.resize(p);
        s.trim(" \t", 'r');
    }
    return "";
}

fastring getline(co::vector<fastring>& lines, size_t& n) {
    fastring line;
    while (n < lines.size()) {
        auto& x = lines[n++];
        x.replace("　", " ");  // replace Chinese spaces
        x.trim();
        if (!x.empty() && !x.starts_with('#')) {
            if (!x.ends_with('\\')) {
                line += x;
                return line;
            }
            x.resize(x.size() - 1);
            x.trim(" \t\r\n", 'r');
            line += x;
        } else {
            if (!line.empty()) return line;
        }
    }
    return line;
}

void Mod::parse_config(const fastring& config) {
    fs::file f(config, 'r');
    if (!f) {
        co::cout(MLS_e_open_failed, ": ", config, co::endl);
        ::exit(0);
    }

    fastring data = f.read((size_t)f.size());
    char sep = '\n';
    if (data.find('\n') == data.npos && data.find('\r') != data.npos) sep = '\r';

    auto lines = str::split(data, sep);
    size_t lineno = 0;

    for (size_t i = 0; i < lines.size();) {
        lineno = i;
        fastring s = getline(lines, i);
        if (s.empty()) continue;

        size_t p = s.find('=');
        if (p == 0 || p == s.npos) {
            co::cout(MLS_e_conf, ": ", s, "  (", config, ':', lineno + 1, ')', co::endl);
            ::exit(0);
        }

        fastring flg = str::trim(s.substr(0, p), " \t", 'r');
        fastring val = str::trim(s.substr(p + 1), " \t", 'l');
        const char* e = remove_quotes_and_comments(val);
        if (*e) {
            co::cout(e, "  (", config, ':', lineno + 1, ')', co::endl);
            ::exit(0);
        }

        val.unescape();
        if (!this->find_flag(flg.c_str())) {
            co::cout(
                color::yellow, "WARNING: ", color::deflt,
                MLS_e_not_found, ": ", flg,
                "  (", config, ':', lineno + 1, ')', co::endl
            );
        } else {
            fastring e = this->set_flag_value(flg.c_str(), val);
            if (!e.empty()) {
                co::cout(e, "  (", config, ':', lineno + 1, ')', co::endl);
                ::exit(0);
            }
        }
    }
}

void _add_flag(
    char iden, const char* name, const char* value, const void* help, 
    const char* file, int line, void* addr, const char* alias, bool mls
) {
    auto f = co::_make_static<Flag>();
    f->iden = iden;
    f->attr = (char)flag::attr_default;
    f->mls = mls;
    f->name = name;
    f->alias = alias;
    f->value = value;
    f->file = file;
    f->line = line;
    f->addr = addr;

    const char* h;
    if (!mls) {
        f->help = (const char*)help;
        h = f->help;
    } else {
        f->mlp = (const co::mlstr*)help;
        h = f->mlp->value();
    }
    if (*h == '@') {
        const char c = *(h + 1);
        if (c == 'c' || c == 'h') f->attr = c;
    }

    mod().add_flag(f);
}


void add_flag(
    char iden, const char* name, const char* value, const char* help, 
    const char* file, int line, void* addr, const char* alias) {
    _add_flag(iden, name, value, help, file, line, addr, alias, false);
}

void add_flag(
    char iden, const char* name, const char* value, const co::mlstr& help, 
    const char* file, int line, void* addr, const char* alias) {
    _add_flag(iden, name, value, &help, file, line, addr, alias, true);
}

} // namespace xx

fastring alias(const char* name, const char* new_name) {
    return xx::mod().alias(name, new_name);
}

void set_config_path(const char* path) {
    xx::mod().set_config_path(path);
}

void set_version(const char* ver) {
    xx::mod().set_version(ver);
}

fastring set_attr(const char* name, _attr_t a) {
    return xx::mod().set_flag_attr(name, a);
}

fastring set_value(const char* name, const fastring& value) {
    return xx::mod().set_flag_value(name, value);
}

void run_after_parse(void(*cb)()) {
    xx::mod().add_parse_cb(cb, 'a');
}

// add a callback to be called before command line args are parsed
void run_before_parse(void(*cb)()) {
    xx::mod().add_parse_cb(cb, 'b');
}

co::vector<fastring> parse(int argc, char** argv, bool command_line_only) {
    auto& mod = xx::mod();
    mod.run_parse_cb('b');

    g_command_line_only = command_line_only;
    auto v = mod.parse_commandline(argc, argv);
    if (FLG_mkconf) {
        mod.make_config(argv[0]);
        ::exit(0);
    }

    mod.run_parse_cb('a');
    return v;
}

} // flag
