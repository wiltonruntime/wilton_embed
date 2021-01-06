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
 * File:   wilton_embed.h
 * Author: alex
 *
 * Created on January 03, 2021, 6:47 PM
 */

#ifndef WILTON_EMBED_H
#define WILTON_EMBED_H

#include "wilton/wilton.h"

#ifdef __cplusplus
extern "C" {
#endif

char* wilton_embed_alloc(
        int size_bytes);

void wilton_embed_free(
        char* buffer);

char* wilton_embed_init(
        const char* wilton_home,
        int wilton_home_len,
        const char* script_engine,
        int script_engine_len,
        const char* app_dir,
        int app_dir_len);

char* wilton_embed_shutdown();

char* wilton_embed_call(
        const char* call_name,
        int call_name_len,
        const char* json_in,
        int json_in_len,
        char** json_out,
        int* json_out_len);

#ifdef __cplusplus
}
#endif

#endif /* WILTON_EMBED_H */
