-- plat
set_config("plat", os.host())

-- project
set_project("co")

-- set xmake minimum version
set_xmakever("2.5.6")

-- set common flags
set_languages("c++17")
set_warnings("all")     -- -Wall
set_symbols("debug")    -- dbg symbols

if is_plat("windows") then
    set_optimize("fastest")
    add_cxflags("/EHsc")
    add_ldflags("/SAFESEH:NO")
elseif is_plat("mingw") then
    add_ldflags("-static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic", {force = true})
    set_optimize("faster")
else
    set_optimize("faster")  -- faster: -O2  fastest: -O3  none: -O0
    --add_cxflags("-Wno-narrowing", "-Wno-sign-compare", "-Wno-strict-aliasing")
    if is_plat("macosx", "iphoneos") then
        add_cxflags("-fno-pie")
    end
end

option("with_backtrace")
    set_default(false)
    set_showmenu(true)
    set_description("build with libbacktrace, for linux only")
option_end()

option("co_debug_log")
    set_default(false)
    set_showmenu(true)
    set_description("print debug log for co")
option_end()

-- include dir
add_includedirs("include")

-- include sub-projects
includes("src", "test", "unitest")
