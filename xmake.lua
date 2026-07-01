-- 定义项目
set_project("tiny-lsm")
set_version("0.0.1")
set_languages("c++20")

add_rules("mode.debug", "mode.release", "mode.coverage")
set_defaultmode("release")

-- 在 coverage 模式下设置 flags
if is_mode("coverage") then
    add_cxxflags("--coverage")
    add_ldflags("--coverage")
end

add_repositories("local-repo build")

add_requires("gtest")
add_requires("asio")
add_requires("pybind11")
add_requires("spdlog", { system = false })
add_requires("toml11", { system = false })
add_requires("crc32c", { system = false })

if is_mode("debug") then
    add_defines("LSM_DEBUG")
end

target("logger")
    set_kind("static")
    add_files("src/logger/*.cpp")
    add_packages("spdlog")
    add_includedirs("include", {public = true})

target("config")
    set_kind("static")
    add_files("src/config/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("utils")
    set_kind("static")
    add_files("src/utils/*.cpp")
    add_packages("toml11", "spdlog", "crc32c")
    add_includedirs("include", {public = true})

target("vlog")
    set_kind("static")
    add_deps("utils", "config")
    add_files("src/vlog/*.cpp")
    add_packages("toml11", "spdlog", "crc32c")
    add_includedirs("include", {public = true})

target("iterator")
    set_kind("static")
    add_files("src/iterator/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("skiplist")
    set_kind("static")
    add_files("src/skiplist/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("memtable")
    set_kind("static")
    add_deps("skiplist", "iterator", "config", "sst")
    add_packages("toml11", "spdlog")
    add_files("src/memtable/*.cpp")
    add_includedirs("include", {public = true})

target("block")
    set_kind("static")
    add_deps("config")
    add_files("src/block/*.cpp")
    add_packages("toml11", "spdlog", "crc32c")
    add_includedirs("include", {public = true})

target("sst")
    set_kind("static")
    add_deps("block", "utils", "iterator", "vlog")
    add_files("src/sst/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("wal")
    set_kind("static")
    add_deps("sst", "memtable")
    add_files("src/wal/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("lsm")
    set_kind("static")
    add_deps("sst", "memtable", "wal", "logger")
    add_files("src/lsm/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("redis")
    set_kind("static")
    add_deps("lsm")
    add_files("src/redis_wrapper/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

-- ============ 共享库目标（供外部使用） ============

target("lsm_shared")
    set_kind("shared")
    add_files("src/logger/*.cpp", "src/config/*.cpp", "src/utils/*.cpp",
              "src/vlog/*.cpp",
              "src/iterator/*.cpp", "src/skiplist/*.cpp", "src/memtable/*.cpp",
              "src/block/*.cpp", "src/sst/*.cpp", "src/wal/*.cpp", "src/lsm/*.cpp",
              "src/redis_wrapper/*.cpp")
    add_packages("toml11", "spdlog", "crc32c")
    add_includedirs("include", {public = true})  -- 确保包含路径正确
    set_targetdir("$(buildir)/lib")

    if is_plat("windows") then
        set_extension(".dll")
        add_defines("TINYLSM_EXPORTS")
        add_cxxflags("/LD")
    else
        set_extension(".so")
    end

    on_install(function (target)
        os.cp("include", path.join(target:installdir(), "include/tiny-lsm"))
        local libfile = target:targetfile()
        if is_plat("windows") then
            os.cp(libfile, path.join(target:installdir(), "bin"))
            local implib = path.join(path.directory(libfile), target:name() .. ".lib")
            if os.isfile(implib) then
                os.cp(implib, path.join(target:installdir(), "lib"))
            end
        else
            os.cp(libfile, path.join(target:installdir(), "lib"))
        end
    end)

-- ============ 测试目标 ============

target("test_config")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_config.cpp")
    add_deps("logger", "config")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_skiplist")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_skiplist.cpp")
    add_deps("logger", "skiplist")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_memtable")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_memtable.cpp")
    add_deps("logger", "memtable")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_block")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_block.cpp")
    add_deps("logger", "block")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_blockmeta")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_blockmeta.cpp")
    add_deps("logger", "block")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_utils")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_utils.cpp")
    add_deps("logger", "utils")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_sst")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_sst.cpp")
    add_deps("logger", "sst")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_lsm")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_lsm.cpp")
    add_deps("logger", "lsm", "memtable", "iterator")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_block_cache")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_block_cache.cpp")
    add_deps("logger", "block")
    add_includedirs("include", {public = true})
    add_packages("gtest", "toml11", "spdlog")

target("test_compact")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_compact.cpp")
    add_deps("logger", "lsm", "memtable", "iterator")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_redis")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_redis.cpp")
    add_deps("logger", "redis", "memtable", "iterator")
    add_includedirs("include", {public = true})
    add_packages("gtest", "toml11", "spdlog")

target("test_wal")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_wal.cpp")
    add_deps("logger", "wal", "lsm")
    add_includedirs("include", {public = true})
    add_packages("gtest", "toml11", "spdlog")

target("test_wisckey")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_wisckey.cpp")
    add_deps("logger", "lsm", "memtable", "iterator")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

-- ============ 可执行目标 ============

target("example")
    set_kind("binary")
    add_files("example/main.cpp")
    add_deps("logger", "config", "utils", "iterator", "skiplist",
             "memtable", "block", "sst", "wal", "lsm", "redis")
    add_includedirs("include")  -- 显式添加包含路径
    set_targetdir("$(buildir)/bin")

target("debug")
    set_kind("binary")
    add_files("example/debug.cpp")
    add_deps("logger", "config", "utils", "iterator", "skiplist",
             "memtable", "block", "sst", "wal", "lsm", "redis")
    add_includedirs("include")  -- 显式添加包含路径
    set_targetdir("$(buildir)/bin")

target("server")
    set_kind("binary")
    add_files("server/src/*.cpp")
    add_deps("redis")
    add_includedirs("include", {public = true})
    add_packages("asio")
    set_targetdir("$(buildir)/bin")

-- ============ Python 绑定 ============

-- 根据平台选择合适的lsm_pybind目标
if is_plat("windows") then
    target("lsm_pybind")
        set_kind("shared")
        add_files("sdk/lsm_pybind.cpp")
        add_packages("pybind11")
        add_deps("lsm")  -- Windows下使用原来的依赖
        add_includedirs("include", {public = true})
        set_targetdir("$(buildir)/lib")
        set_filename("lsm_pybind.pyd")
        add_cxxflags("/LD")
else
    -- Unix/Linux/macOS平台
    target("lsm_pybind")
        set_kind("shared")
        add_files("sdk/lsm_pybind.cpp")
        add_packages("pybind11")
        add_deps("lsm_shared")  -- Unix下使用共享库依赖
        add_includedirs("include", {public = true})
        set_targetdir("$(buildir)/lib")
        set_filename("lsm_pybind.so")
        add_ldflags("-Wl,-rpath,$ORIGIN")
        add_defines("TINYLSM_EXPORT=__attribute__((visibility(\"default\")))")
        add_cxxflags("-fvisibility=hidden")
end

-- ============ 测试任务 ============

task("run-all-tests")
    set_category("plugin")
    set_menu {
        usage = "xmake run-all-tests",
        description = "Build and run all test binaries"
    }

    on_run(function ()
        import("core.project.project")

        local targets = project.targets()
        local test_targets = {}

        for name, _ in pairs(targets) do
            if name:startswith("test_") then
                table.insert(test_targets, name)
            end
        end

        table.sort(test_targets)

        if #test_targets == 0 then
            print("\27[33m[Warning] No test targets found.\27[0m")
            return
        end

        for _, name in ipairs(test_targets) do
            print("\27[32m>> Running\27[0m " .. name)
            os.execv("xmake", {"run", name})
            print("")
        end

        print("\27[32mAll tests finished.\27[0m")
    end)


-- ============ lldb for debug ============
local function lldb_context(project, env)
    local function target_names()
        local names = {}
        for name, target in pairs(project.targets()) do
            if target:kind() == "binary" then
                table.insert(names, name)
            end
        end
        table.sort(names)
        return names
    end

    local function resolve_target(target_name)
        target_name = (target_name or ""):gsub("^%s+", ""):gsub("%s+$", "")
        if target_name == "" then
            env.raise("missing target; use -t TARGET. Debuggable targets: %s", table.concat(target_names(), ", "))
        end

        local target = project.target(target_name)
        if not target or target:kind() ~= "binary" then
            env.raise("debuggable target not found: %s. Debuggable targets: %s", target_name, table.concat(target_names(), ", "))
        end

        return target_name, target
    end

    local function program()
        local lldb = env.os.iorunv("sh", {"-c", "command -v lldb 2>/dev/null || command -v lldb-18 2>/dev/null"}):gsub("%s+$", "")
        if lldb == "" then
            env.raise("lldb not found; install lldb or lldb-18")
        end
        return lldb
    end

    local function targetfile(target_name, target)
        local show = env.os.iorunv("xmake", {"show", "-t", target_name})
        show = show:gsub("\27%[[0-9;]*m", "")
        local file = show:match("targetfile:%s*([^\r\n]+)") or target:targetfile()
        if not env.os.isfile(file) then
            env.raise("target file not found: %s", file)
        end
        return file
    end

    return {
        resolve_target = resolve_target,
        program = program,
        targetfile = targetfile
    }
end

local function lldb_run(project, env, target_name, commands)
    local lldb = lldb_context(project, env)
    local resolved_target_name, target = lldb.resolve_target(target_name)
    local program = lldb.program()

    local function shell_quote(value)
        return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
    end

    local lldb_args = ""
    if commands then
        for _, arg in ipairs(commands) do
            lldb_args = lldb_args .. " " .. shell_quote(arg)
        end
    end

    local script = table.concat({
        "set -e",
        "xmake f -m debug",
        "trap 'xmake f -m release >/dev/null 2>&1 || true' EXIT",
        "xmake build " .. shell_quote(resolved_target_name),
        "targetfile=$(xmake show -t " .. shell_quote(resolved_target_name) .. " | python3 -c 'import re, sys; text = re.sub(r\"\\x1b\\[[0-9;]*m\", \"\", sys.stdin.read()); match = re.search(r\"targetfile\\s*:\\s*([^\\r\\n]+)\", text); print(match.group(1).strip() if match else \"\")')",
        "if [ -z \"$targetfile\" ]; then echo 'target file not found' >&2; exit 1; fi",
        shell_quote(program) .. lldb_args .. " -- \"$targetfile\""
    }, " && ")

    env.os.execv("sh", {"-c", script})
end

task("lldb")
    set_category("plugin")
    set_menu {
        usage = "xmake lldb [-t|--target TARGET]",
        description = "Build a target and run it under LLDB",
        options = {
            {'t', "target", "kv", "", "Binary target to debug"}
        }
    }

    on_run(function ()
        import("core.project.project")
        import("core.base.option")

        lldb_run(project, {os = os, raise = raise}, option.get("target"))
    end)

task("lldb-bt")
    set_category("plugin")
    set_menu {
        usage = "xmake lldb-bt [-t|--target TARGET]",
        description = "Build a target, run it under LLDB, and print a backtrace on crash",
        options = {
            {'t', "target", "kv", "", "Binary target to debug"}
        }
    }

    on_run(function ()
        import("core.project.project")
        import("core.base.option")

        lldb_run(project, {os = os, raise = raise}, option.get("target"), {"-b", "-o", "run", "-k", "bt"})
    end)
