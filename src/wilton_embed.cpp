/*
 * Copyright 2021, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * File:   wilton_embed.cpp
 * Author: alex
 *
 * Created on January 03, 2021, 6:47 PM
 */

#include "wilton/wilton_embed.h"

#include <iostream>
#include <string>

#include "staticlib/config.hpp"

#if defined(STATICLIB_LINUX)
#include <unistd.h>
#elif defined(STATICLIB_MAC)
extern char** environ;
#endif

#include "utf8.h"

#include "staticlib/io.hpp"
#include "staticlib/json.hpp"
#include "staticlib/support.hpp"
#ifdef STATICLIB_WINDOWS
#include "staticlib/support/windows.hpp"
#endif // STATICLIB_WINDOWS
#include "staticlib/tinydir.hpp"
#include "staticlib/utils.hpp"
#include "staticlib/unzip.hpp"

#include "wilton/wiltoncall.h"
#include "wilton/wilton_signal.h"

#include "wilton/support/alloc.hpp"
#include "wilton/support/exception.hpp"
#include "wilton/support/misc.hpp"

#define WILTON_QUOTE(value) #value
#define WILTON_STR(value) WILTON_QUOTE(value)
#ifndef WILTON_VERSION
#define WILTON_VERSION UNSPECIFIED
#endif // !WILTON_VERSION
#define WILTON_VERSION_STR WILTON_STR(WILTON_VERSION)

namespace { // anonymous

std::string get_env_var_value(const std::vector<std::string>& parts) {
    if (parts.size() < 2) throw wilton::support::exception(TRACEMSG(
            "Invalid environment variable vector specified," +
            " parts count: [" + sl::support::to_string(parts.size()) + "]"));
    auto value = sl::utils::trim(parts.at(1));
    for (size_t i = 2; i < parts.size(); i++) {
        value.push_back('=');
        value += sl::utils::trim(parts.at(i));
    }
    return value;
}

std::vector<sl::json::field> collect_env_vars() {
#ifdef STATICLIB_WINDOWS
    auto envp = _environ;
#else // !STATICLIB_WINDOWS
    auto envp = environ;
#endif
    auto vec = std::vector<sl::json::field>();
    for (char** el = envp; *el != nullptr; el++) {
        auto var = std::string(*el);
        auto parts = sl::utils::split(var, '=');
        if (parts.size() >= 2) {
            auto name = sl::utils::trim(parts.at(0));
            auto name_clean = std::string();
            utf8::replace_invalid(name.begin(), name.end(), std::back_inserter(name_clean));
            auto value = get_env_var_value(parts);
            auto value_clean = std::string();
            utf8::replace_invalid(value.begin(), value.end(), std::back_inserter(value_clean));
            vec.emplace_back(std::move(name_clean), std::move(value_clean));
        }
    }
    std::sort(vec.begin(), vec.end(), [](const sl::json::field& a, const sl::json::field& b) {
        return a.name() < b.name();
    });
    return vec;
}

void validate_paths(const std::string& wilton_home, const std::string& app_dir) {
    auto wilton_home_path = sl::tinydir::path(wilton_home);
    if (!(wilton_home_path.exists() && wilton_home_path.is_directory())) {
        throw wilton::support::exception(TRACEMSG("Specified WILTON_HOME directory not found," +
                " path: [" + wilton_home_path.filepath() + "]"));
    }
    auto app_dir_path = sl::tinydir::path(app_dir);
    if (!(app_dir_path.exists() && app_dir_path.is_directory())) {
        throw wilton::support::exception(TRACEMSG("Specified application directory not found," +
                " path: [" + app_dir_path.filepath() + "]"));
    }
}

void dyload_module(const std::string& wilton_home, const std::string& name) {
    auto bin_dir = wilton_home + "/bin/";
    auto err_dyload = wilton_dyload(name.c_str(), static_cast<int>(name.length()),
            bin_dir.c_str(), static_cast<int>(bin_dir.length()));
    if (nullptr != err_dyload) {
        wilton::support::throw_wilton_error(err_dyload, TRACEMSG(err_dyload));
    }
}

std::vector<sl::json::value> load_packages_list(const std::string& wilton_home,
        const std::string& stdlib_url) {
    // note: cannot use 'wilton_loader' here, as it is not initialized yet
    dyload_module(wilton_home, "wilton_zip");
    auto packages_json_id = "wilton-requirejs/wilton-packages.json";
    auto zip_path = stdlib_url.substr(wilton::support::zip_proto_prefix.length());
    auto idx = sl::unzip::file_index(zip_path);
    sl::unzip::file_entry en = idx.find_zip_entry(packages_json_id);
    if (en.is_empty()) throw wilton::support::exception(TRACEMSG(
            "Unable to load 'wilton-packages.json', ZIP entry: [" + packages_json_id + "]," +
            " file: [" + zip_path + "]"));
    auto stream = sl::unzip::open_zip_entry(idx, packages_json_id);
    auto src = sl::io::streambuf_source(stream->rdbuf());
    auto json = sl::json::load(src);
    return std::move(json.as_array_or_throw(packages_json_id));
}

sl::support::optional<sl::json::value> load_app_config(const std::string& appdir) {
    if (!appdir.empty()) {
        auto pconf = sl::tinydir::path(appdir + "conf/");
        if (pconf.exists()) {
            auto cfile = sl::tinydir::path(appdir + "conf/config.json");
            if (cfile.exists() && !cfile.is_directory()) {
                auto src = cfile.open_read();
                auto val = sl::json::load(src);
                return sl::support::make_optional(std::move(val));
            }
        }
    }
    return sl::support::optional<sl::json::value>();
}

std::string application_name(const std::string& appdir) {
    auto conf = load_app_config(appdir);
    if (conf.has_value()) {
        auto& json = conf.value();
        return json["appname"].as_string_nonempty_or_throw("conf/config.json:appname");
    }
    return sl::utils::strip_parent_dir(appdir);
}

std::string wilton_executable(const std::string& wilton_home) {
    auto path = wilton_home + "/bin/wilton";
#ifdef STATICLIB_WINDOWS
    path += "w.exe";
#endif
    return path;
}

std::vector<sl::json::field> prepare_paths(const std::string& wilton_home,
        const std::string& app_name, const std::string& app_dir) {
    std::vector<sl::json::field> res;
    // startup module
    res.emplace_back(app_name, wilton::support::file_proto_prefix + sl::tinydir::full_path(app_dir));
    // vendor libs
    auto libdir = sl::tinydir::path(wilton_home + "/lib");
    if (libdir.exists()) {
        for (sl::tinydir::path& libpath : sl::tinydir::list_directory(libdir.filepath())) {
            if (libpath.is_directory()) {
                auto& modname = libpath.filename();
                res.emplace_back(modname, wilton::support::file_proto_prefix + libpath.filepath());
            } else if (sl::utils::ends_with(libpath.filename(), ".js")) {
                const auto& modname = libpath.filename().substr(0, libpath.filename().length() - 3);
                const auto& dirpath = sl::utils::strip_filename(libpath.filepath());
                res.emplace_back(modname, wilton::support::file_proto_prefix + dirpath + modname);
            } else if (sl::utils::ends_with(libpath.filename(), ".wlib")) {
                const auto& modname = libpath.filename().substr(0, libpath.filename().length() - 5);
                res.emplace_back(modname, wilton::support::zip_proto_prefix + libpath.filepath());
            }
        }
    }
    return res;
}

void init_signals(const std::string& wilton_home) {
    dyload_module(wilton_home, "wilton_signal");
    auto err_init = wilton_signal_initialize();
    if (nullptr != err_init) {
        auto msg = TRACEMSG(err_init);
        wilton_free(err_init);
        throw wilton::support::exception(msg);
    }
#ifdef STATICLIB_WINDOWS
    // https://stackoverflow.com/a/9719240/314015
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif // STATICLIB_WINDOWS
}

} // namespace

char* wilton_embed_alloc(int size_bytes) /* noexcept */ {
    return wilton_alloc(size_bytes);
}

void wilton_embed_free(char* buffer) /* noexcept */ {
    wilton_free(buffer);
}

char* wilton_embed_init(const char* wilton_home, int wilton_home_len,
        const char* script_engine, int script_engine_len,
        const char* app_dir, int app_dir_len) /* noexcept */ {
    if (nullptr == wilton_home) return wilton::support::alloc_copy(TRACEMSG("Null 'wilton_home' parameter specified"));
    if (!sl::support::is_uint16_positive(wilton_home_len)) return wilton::support::alloc_copy(TRACEMSG(
            "Invalid 'wilton_home_len' parameter specified: [" + sl::support::to_string(wilton_home_len) + "]"));
    if (nullptr == script_engine) return wilton::support::alloc_copy(TRACEMSG("Null 'script_engine' parameter specified"));
    if (!sl::support::is_uint16_positive(script_engine_len)) return wilton::support::alloc_copy(TRACEMSG(
            "Invalid 'script_engine_len' parameter specified: [" + sl::support::to_string(script_engine_len) + "]"));
    if (nullptr == app_dir) return wilton::support::alloc_copy(TRACEMSG("Null 'app_dir' parameter specified"));
    if (!sl::support::is_uint16_positive(app_dir_len)) return wilton::support::alloc_copy(TRACEMSG(
            "Invalid 'app_dir_len' parameter specified: [" + sl::support::to_string(app_dir_len) + "]"));
    try {
        // params and paths
        auto wilton_home_str = std::string(wilton_home, static_cast<uint16_t>(wilton_home_len));
        auto script_engine_str = std::string(script_engine, static_cast<uint16_t>(script_engine_len));
        auto app_dir_str = std::string(app_dir, static_cast<uint16_t>(app_dir_len));

        // paths and urls
        validate_paths(wilton_home_str, app_dir_str);
        auto stdlib_url = wilton::support::zip_proto_prefix + sl::tinydir::full_path(wilton_home_str + "/std.wlib");
        auto app_dir_url = wilton::support::file_proto_prefix + sl::tinydir::full_path(app_dir_str);
        auto app_name_str = application_name(app_dir_str);

        // packages
        auto packages = load_packages_list(wilton_home_str, stdlib_url);


        // env vars
        auto env_vars = collect_env_vars();
        auto env_vars_pairs = std::vector<std::pair<std::string, std::string>>();
        for (auto& fi : env_vars) {
            env_vars_pairs.emplace_back(fi.name(), fi.as_string_or_throw(fi.name()));
        }

        // config
        auto config = sl::json::dumps({
            {"defaultScriptEngine", script_engine_str},
            {"wiltonExecutable", wilton_executable(wilton_home_str)},
            {"wiltonHome", sl::tinydir::full_path(wilton_home_str) + "/"},
            {"wiltonVersion", WILTON_VERSION_STR},
            {"requireJs", {
                    {"waitSeconds", 0},
                    {"enforceDefine", true},
                    {"nodeIdCompat", true},
                    {"baseUrl", stdlib_url},
                    {"paths", prepare_paths(wilton_home_str, app_name_str, app_dir_str)},
                    {"packages", std::move(packages)}
                }
            },
            {"environmentVariables", std::move(env_vars)},
// add compile-time OS
#if defined(STATICLIB_ANDROID)
            {"compileTimeOS", "android"},
#elif defined(STATICLIB_WINDOWS)
            {"compileTimeOS", "windows"},
#elif defined(STATICLIB_LINUX)
            {"compileTimeOS", "linux"},
#elif defined(STATICLIB_MAC)
            {"compileTimeOS", "macos"}
#endif // OS
        });
        // std::cerr << config << std::endl;

        // init
        auto err_init = wiltoncall_init(config.c_str(), static_cast<int> (config.length()));
        if (nullptr != err_init) {
            return err_init;
        }
        dyload_module(wilton_home_str, "wilton_logging");
        dyload_module(wilton_home_str, "wilton_loader");
        dyload_module(wilton_home_str, "wilton_" + script_engine_str);
        init_signals(wilton_home_str);
        std::cerr << "signals" << std::endl;

        return nullptr;
    } catch (const std::exception& e) {
        return wilton::support::alloc_copy(TRACEMSG(e.what() + "\nException raised"));
    }
}

char* wilton_embed_shutdown() /* noexcept */ {
    std::cout << "wilton_embed_shutdown" << std::endl;
    return nullptr;
}

char* wilton_embed_call(const char* call_name, int call_name_len,
        const char* json_in, int json_in_len,
        char** json_out, int* json_out_len) /* noexcept */ {
    return wiltoncall(call_name, call_name_len, json_in, json_in_len, json_out, json_out_len);
}