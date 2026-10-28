# ##############################################################################
# libs/libc/machine/loongarch/gnu/CMakeLists.txt
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

set(SRCS)

if(CONFIG_LOONGARCH_MEMCPY)
  list(APPEND SRCS arch_memcpy.S)
endif()

if(CONFIG_LOONGARCH_MEMSET)
  list(APPEND SRCS arch_memset.S)
endif()

if(CONFIG_LOONGARCH_MEMMOVE)
  list(APPEND SRCS arch_memmove.S)
endif()

if(CONFIG_LOONGARCH_MEMCMP)
  list(APPEND SRCS arch_memcmp.S)
endif()

if(CONFIG_LOONGARCH_MEMCHR)
  list(APPEND SRCS arch_memchr.S)
endif()

if(CONFIG_LOONGARCH_STRLEN)
  list(APPEND SRCS arch_strlen.S)
endif()

if(CONFIG_LOONGARCH_STRNLEN)
  list(APPEND SRCS arch_strnlen.S)
endif()

if(CONFIG_LOONGARCH_STRCMP)
  list(APPEND SRCS arch_strcmp.S)
endif()

if(CONFIG_LOONGARCH_STRNCMP)
  list(APPEND SRCS arch_strncmp.S)
endif()

if(CONFIG_LOONGARCH_STRCPY)
  list(APPEND SRCS arch_strcpy.S)
endif()

if(CONFIG_LOONGARCH_STRCHR)
  list(APPEND SRCS arch_strchr.S)
endif()

if(CONFIG_LOONGARCH_STRCHRNUL)
  list(APPEND SRCS arch_strchrnul.S)
endif()

if(CONFIG_LOONGARCH_STRRCHR)
  list(APPEND SRCS arch_strrchr.S)
endif()

if(CONFIG_ARCH_SETJMP_H)
  list(APPEND SRCS arch_setjmp.S)
endif()

if(CONFIG_BUILD_KERNEL)
  list(APPEND SRCS arch_signal.S)
endif()

target_sources(c PRIVATE ${SRCS})
