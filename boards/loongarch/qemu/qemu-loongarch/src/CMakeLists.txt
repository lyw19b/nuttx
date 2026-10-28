# ##############################################################################
# boards/loongarch/qemu/qemu-loongarch/src/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

target_sources(board PRIVATE qemu_boot.c)

set_property(GLOBAL PROPERTY LD_SCRIPT
             "${NUTTX_BOARD_DIR}/scripts/ld.script")

if(CONFIG_LOONGARCH_USER_TEST)
  set(user_dir ${CMAKE_CURRENT_BINARY_DIR}/user)
  file(MAKE_DIRECTORY ${user_dir})
  set(user_src ${CMAKE_CURRENT_LIST_DIR}/../tests)
  set(user_includes -I${CMAKE_BINARY_DIR}/include -I${NUTTX_DIR}/include)
  add_custom_command(
    OUTPUT ${user_dir}/user.elf
    COMMAND ${CMAKE_C_COMPILER} ${user_includes} -E -P -x c
            ${user_src}/user.ld -o ${user_dir}/user.ld
    COMMAND ${CMAKE_C_COMPILER} ${user_includes} -D__ASSEMBLY__ -c
            ${NUTTX_DIR}/libs/libc/machine/loongarch/arch_signal.S
            -o ${user_dir}/signal.o
    COMMAND ${CMAKE_C_COMPILER} ${user_includes} -march=loongarch64
            -mabi=lp64d -mstrict-align -O2 -ffreestanding -fno-stack-protector
            -fno-pic -fno-pie -no-pie
            -nostdlib -static -Wl,--build-id=none -Wl,-T,${user_dir}/user.ld
            ${user_src}/user.c ${user_dir}/signal.o
            -o ${user_dir}/user.elf
    DEPENDS ${user_src}/user.c ${user_src}/user.ld
            ${CMAKE_BINARY_DIR}/include/nuttx/config.h
            ${NUTTX_DIR}/libs/libc/machine/loongarch/arch_signal.S
            ${NUTTX_DIR}/arch/loongarch/include/syscall.h)
  add_custom_target(loongarch_user_elf DEPENDS ${user_dir}/user.elf)
  add_dependencies(board loongarch_user_elf)
  set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/qemu_user_image.S TARGET_DIRECTORY board
    PROPERTIES COMPILE_DEFINITIONS
    "LOONGARCH_USER_IMAGE=\"${user_dir}/user.elf\""
    OBJECT_DEPENDS ${user_dir}/user.elf)
  target_sources(board PRIVATE qemu_usertest.c qemu_user_image.S)
endif()
