############################################################################
# boards/loongarch/qemu/qemu-loongarch/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(TOPDIR)/Make.defs

CSRCS = qemu_boot.c

ifeq ($(CONFIG_LOONGARCH_USER_TEST),y)
CSRCS += qemu_usertest.c
ASRCS += qemu_user_image.S
AFLAGS += -DLOONGARCH_USER_IMAGE='"$(CURDIR)/user.elf"'
endif

include $(TOPDIR)/boards/Board.mk

ifeq ($(CONFIG_LOONGARCH_USER_TEST),y)
USER_TEST_DIR = $(BOARD_DIR)/tests
USER_ARCH_DIR = $(TOPDIR)/arch/loongarch/src/common
USER_TEST_LIBS = $(addprefix $(TOPDIR)/staging/,libc.a libmm.a libproxies.a)
USER_TEST_FLAGS = -I$(TOPDIR)/include -march=loongarch64 -mabi=lp64d
USER_TEST_FLAGS += -mstrict-align -O2 -ffreestanding -fno-stack-protector
USER_TEST_FLAGS += -fno-pic -fno-pie -U__KERNEL__

user.ld: $(USER_ARCH_DIR)/loongarch_user.ld $(TOPDIR)/include/nuttx/config.h
	$(Q) $(CC) -I$(TOPDIR)/include -E -P -x c $< -o $@

user.elf: $(USER_TEST_DIR)/user.c user.ld $(USER_ARCH_DIR)/crt0.c \
          $(TOPDIR)/arch/loongarch/include/syscall.h $(USER_TEST_LIBS)
	$(Q) $(CC) $(USER_TEST_FLAGS) -no-pie -nostdlib -static \
	  -Wl,--build-id=none -Wl,-T,user.ld $(USER_ARCH_DIR)/crt0.c $< \
	  -Wl,--start-group $(USER_TEST_LIBS) -lgcc -Wl,--end-group -o $@

qemu_user_image$(OBJEXT): user.elf

clean::
	$(call DELFILE, user.elf)
	$(call DELFILE, user.ld)
endif
