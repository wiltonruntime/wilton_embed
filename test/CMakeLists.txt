
# Copyright 2021, alex at staticlibs.net
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required ( VERSION 2.8.12 )

# project
project ( wilton_embed_test CXX )

add_executable ( ${PROJECT_NAME} wilton_embed_test.cpp )

if (WIN32)
    target_compile_definitions ( ${PROJECT_NAME} PRIVATE -D_CRT_SECURE_NO_WARNINGS )
else ()
    target_link_libraries ( ${PROJECT_NAME} dl )
endif ()