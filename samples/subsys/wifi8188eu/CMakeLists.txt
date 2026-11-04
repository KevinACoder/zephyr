# SPDX-License-Identifier: Apache-2.0
#
# The image is built out of the freewificam module: its `main.c` is the entry
# (console banner, kernel facade, case runner) and its platform library carries
# the CMSIS-RTOS2 backend.  There is therefore no source of this sample's own -
# an application `main()` here would collide with the one the module provides.
#
# The sample exists to pin the configuration: the Kconfig surface of the module
# is three strings, and everything else (the case ladder, the wireless line, the
# USB controller list, the AP identity) comes from the freewificam profile named
# in prj.conf.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi8188eu)

# One translation unit: a build-identity string.  The image's `main()` belongs to
# the freewificam module's platform library (see src/identity.c).
target_sources(app PRIVATE src/identity.c)
