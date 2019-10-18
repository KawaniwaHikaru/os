################################################################################
#
#   Copyright (c) 2012 Minoca Corp.
#
#    This file is licensed under the terms of the GNU General Public License
#    version 3. Alternative licensing terms are available. Contact
#    info@minocacorp.com for details. See the LICENSE file at the root of this
#    project for complete licensing information.
#
#   Binary Name:
#
#       Kernel Debugging Extensions
#
#   Abstract:
#
#       This module implements kernel debugger extensions.
#
#   Author:
#
#       Evan Green 10-Sep-2012
#
#   Environment:
#
#       Debug Client
#
################################################################################

BINARY = kexts.so

BINARYTYPE = so

BINPLACE = bin

TARGETLIBS = $(BINROOT)/dbgext.a

OBJS = acpiext.o  \
       kexts.o    \
       memory.o   \
       objects.o  \
       reslist.o  \
       threads.o  \

ifeq ($(OS),Windows_NT)

DIRS = win32

else

DIRS = uos

endif

include $(SRCROOT)/os/minoca.mk

