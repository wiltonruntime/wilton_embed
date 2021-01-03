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
 * File:   wilton_embed_test.cpp
 * Author: alex
 *
 * Created on January 03, 2021, 6:47 PM
 */

#include <stdlib.h>

#include <dlfcn.h>

#include <iostream>
#include <stdexcept>
#include <string>

typedef char*(*wilton_embed_init_fun)(const char*, int, const char*, int, const char*, int);
typedef char*(*wiltoncall_fun)(const char*, int, const char*, int, char**, int*);
typedef void*(*wilton_free_fun)(char*);

std::string dlerr_str() {
    auto res = ::dlerror();
    return nullptr != res ? std::string(res) : "";
}

void* load_library(const std::string& path) {
    auto lib = ::dlopen(path.c_str(), RTLD_LAZY);
    if (nullptr == lib) {
        throw std::runtime_error(
                "Error loading shared library on path: [" + path + "]," +
                " error: [" + dlerr_str() + "]");
    }
    return lib;
}

void* find_symbol(void* lib, const std::string& name) {
    auto sym = ::dlsym(lib, name.c_str());
    if (nullptr == sym) {
        throw std::runtime_error(
                "Error loading symbol: [" + name + "], " +
                " error: [" + dlerr_str() + "]");
    }
    return sym;
}

int main() {
    auto whome = std::string(getenv("WILTON_HOME"));
    auto engine = std::string("quickjs");
    auto embed_lib = load_library(whome + "/bin/libwilton_embed.so");
    auto embed_init_fun = reinterpret_cast<wilton_embed_init_fun>(find_symbol(embed_lib, "wilton_embed_init"));
    auto core_lib = load_library(whome + "/bin/libwilton_core.so");
    auto wiltoncall = reinterpret_cast<wiltoncall_fun>(find_symbol(core_lib, "wiltoncall"));
    auto wilton_free = reinterpret_cast<wilton_free_fun>(find_symbol(core_lib, "wilton_free"));

    auto call_runscript = std::string("runscript_quickjs");
    auto call_desc_json = std::string(R"({"module": "server/index"})");
    auto app_dir = whome + "/examples/server";

    auto err_init = embed_init_fun(whome.data(), static_cast<int>(whome.length()),
            engine.data(), static_cast<int>(engine.length()),
            app_dir.data(), static_cast<int>(app_dir.length()));
    if (nullptr != err_init) {
        std::cout << std::string(err_init) << std::endl;
        wilton_free(err_init);
        return 1;
    }

    char* json_out = nullptr;
    int json_out_len = -1;
    auto err = wiltoncall(call_runscript.data(), static_cast<int>(call_runscript.length()),
            call_desc_json.data(), static_cast<int>(call_desc_json.length()),
            std::addressof(json_out), std::addressof(json_out_len));

    if (nullptr != err) {
        std::cout << std::string(err) << std::endl;
        wilton_free(err);
        return 1;
    }

    std::cout << "TEST_PASSED" << std::endl;
    return 0;
}