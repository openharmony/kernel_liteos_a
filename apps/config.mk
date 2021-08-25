# Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
# Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other materials
#    provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be used
#    to endorse or promote products derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

include $(LITEOSTOPDIR)/config.mk

ifeq ($(LOSCFG_COMPILER_CLANG_LLVM), y)
LLVM_SYSROOT := --sysroot=$(SYSROOT_PATH)
endif

# common flags config
BASE_OPTS := -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE
BASE_OPTS += -ffunction-sections -fdata-sections -fno-omit-frame-pointer -fno-common -fno-strict-aliasing
BASE_OPTS += -fstack-protector-strong -Wall -Werror -flto
BASE_OPTS += $(LITEOS_CORE_COPTS) $(LLVM_EXTRA_OPTS) $(LLVM_SYSROOT) $(LITEOS_GCOV_OPTS)

ifeq ($(LOSCFG_COMPILER_CLANG_LLVM), y)
OPTMIZE_OPTS = -Oz
else
OPTMIZE_OPTS = -O2
endif

CFLAGS := -std=c99 -fPIE -fno-exceptions $(BASE_OPTS) $(OPTMIZE_OPTS)
CXXFLAGS := -std=c++11 -fPIE -fexceptions -fpermissive -frtti $(BASE_OPTS) $(OPTMIZE_OPTS)
LDFLAGS  := -pie -Wl,-z,relro,-z,now -O2 $(BASE_OPTS) $(LLVM_EXTRA_LD_OPTS)

# alias variable config
HIDE := @
MAKE := make
RM := rm -rf
CP := cp -rf
MV := mv -f

APP := $(APPSTOPDIR)/app.mk
APP_SUBDIRS :=

##build modules config##

ifeq ($(LOSCFG_SHELL), y)
APP_SUBDIRS += shell
APP_SUBDIRS += mksh
APP_SUBDIRS += toybox
endif

ifeq ($(LOSCFG_USER_INIT_DEBUG), y)
APP_SUBDIRS += init
endif

ifeq ($(LOSCFG_NET_LWIP_SACK_TFTP), y)
APP_SUBDIRS += tftp
endif
