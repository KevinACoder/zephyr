# SPDX-License-Identifier: Apache-2.0
#
# The freewificam wireless stack as a Zephyr module.
#
# The tree is compiled in place: no copy, no sparse checkout.  "Only the code
# this image needs" is the source list below, not a pruned directory - the same
# split the make build makes with sources.mk tags (core / nport / bridge / kpi /
# drv / case / cherry).
#
# Five libraries, one per flag world.  The worlds are not cosmetic: the net80211
# core is NetBSD code that needs GNU C and its own forced-include prelude, the
# linuxkpi seam is GNU C without it, the vendor driver is freestanding ISO C11,
# and CherryUSB is GNU C with its own board header.  Compiling one world with
# another's flags is what produces the subtle breakage this split exists to
# prevent, so each group below is a separate target with its own options.
#
# The image configuration is freewificam's, not Zephyr's: this file runs
# freewificam's own generator for the named profile and force-includes the
# header it writes.  There is no second configuration system here, and the
# Zephyr Kconfig surface is the three strings in Kconfig.

cmake_minimum_required(VERSION 3.20.0)

# --- the tree -------------------------------------------------------------

get_filename_component(FREEWIFICAM_DIR "${ZEPHYR_BASE}/${CONFIG_FREEWIFICAM_PATH}" ABSOLUTE)
if(NOT EXISTS "${FREEWIFICAM_DIR}/components/linuxkpi")
  message(FATAL_ERROR
    "freewificam not found at ${FREEWIFICAM_DIR} (set CONFIG_FREEWIFICAM_PATH)")
endif()

set(FWCAM_PLATFORM "${FREEWIFICAM_DIR}/tests/board/rtos/platform")
set(FWCAM_APP      "${FREEWIFICAM_DIR}/tests/board/rtos/app")
set(FWCAM_BOARD    "${FWCAM_PLATFORM}/board/zephyr")
set(FWCAM_NET      "${FREEWIFICAM_DIR}/components/net80211")
set(FWCAM_LKPI     "${FREEWIFICAM_DIR}/components/linuxkpi")
set(FWCAM_USB      "${FREEWIFICAM_DIR}/third-party/cherryusb")
set(FWCAM_DRV      "${FREEWIFICAM_DIR}/wireless-drivers/${CONFIG_FREEWIFICAM_DRIVER}")

# --- the image configuration ----------------------------------------------

# freewificam's generator runs at configure time.  Its output directory is the
# only place freewificam_config.h may come from; the header is what the C side
# sees, and build_defs.h #errors out when it is missing.
set(FWCAM_KCONFIG_OUT "${CMAKE_CURRENT_BINARY_DIR}/kconfig")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${FREEWIFICAM_DIR}/tools/build/kconfig-gen.py"
          --profile "${CONFIG_FREEWIFICAM_PROFILE}"
          --out "${FWCAM_KCONFIG_OUT}"
  RESULT_VARIABLE fwcam_gen_rc
  OUTPUT_VARIABLE fwcam_gen_out
  ERROR_VARIABLE  fwcam_gen_err
)
if(NOT fwcam_gen_rc EQUAL 0)
  message(FATAL_ERROR
    "freewificam configuration generation failed for profile "
    "'${CONFIG_FREEWIFICAM_PROFILE}':\n${fwcam_gen_out}${fwcam_gen_err}")
endif()
if(NOT EXISTS "${FWCAM_KCONFIG_OUT}/freewificam_config.h")
  message(FATAL_ERROR "freewificam_config.h was not generated in ${FWCAM_KCONFIG_OUT}")
endif()

# --- the forced-include pair ----------------------------------------------

set(FWCAM_SHIELD "${CMAKE_CURRENT_LIST_DIR}/port/zephyr_shield.h")
set(FWCAM_BUILD_DEFS "${FWCAM_PLATFORM}/build_defs.h")

# Order is load-bearing: the shield runs first (it drops the Zephyr CONFIG_*
# names that collide and restores freewificam's own header), then build_defs.h
# turns the profile into the macro world every component source expects.
set(FWCAM_FORCE_INCLUDE
  "-include${FWCAM_SHIELD}"
  "-include${FWCAM_BUILD_DEFS}"
)

# The reference images force-include the C library's four heads into every C
# translation unit (tests/board/rtos/build/Makefile: "CFLAGS := $(BASE_CFLAGS)
# -include stdio.h -include string.h -include strings.h -include stdlib.h").
# The sources were written against that contract - they call printf/abs/memcpy
# without including the header - so the same four are forced here.  They name
# Zephyr's minimal libc, which carries all four.
# Written joined (-includestdio.h) rather than as two words: CMake would keep
# "-include stdio.h" as one argument containing a space, and the driver then
# looks for a file literally named " stdio.h".
set(FWCAM_LIBC_FORCE_INCLUDE
  -includestdio.h
  -includestring.h
  -includestrings.h
  -includestdlib.h
)

# The two names Zephyr's C library also defines, renamed for the worlds that
# define their own (see port/libc_names.h).
set(FWCAM_LIBC_NAMES "${CMAKE_CURRENT_LIST_DIR}/port/libc_names.h")

# --- the linker sections ---------------------------------------------------

# Two fragments, two placement rules:
#  - the registries are read-only data and go with ROM (see the file's comment
#    for why "SECTIONS" - the very end of the arm64 script - is wrong here);
#  - the DMA scratch area is writable and goes with .bss.
zephyr_linker_sources(ROM_SECTIONS "${CMAKE_CURRENT_LIST_DIR}/port/freewificam_sections.ld")
zephyr_linker_sources(BSS "${CMAKE_CURRENT_LIST_DIR}/port/freewificam_nocache.ld")

# --- include root ----------------------------------------------------------

# Every world sees these: the platform seam headers (os/cmsis2, arch/aarch64,
# board/zephyr), the generated configuration and the NetBSD compat root.
set(FWCAM_COMMON_INCLUDES
  "${FWCAM_PLATFORM}"
  "${FWCAM_PLATFORM}/os/cmsis2"
  "${FWCAM_PLATFORM}/os/cmsis2/board-compat"
  "${FWCAM_PLATFORM}/board/zephyr"
  "${FWCAM_PLATFORM}/arch/aarch64"
  "${FWCAM_PLATFORM}/os/baremetal"
  "${FWCAM_APP}"
  "${FREEWIFICAM_DIR}/third-party/ulog/src"
  "${FREEWIFICAM_DIR}/third-party/CMSIS_6/CMSIS/RTOS2/Include"
  "${FWCAM_KCONFIG_OUT}"
)

# Headers Zephyr's minimal libc does not ship (<sys/time.h>, <sys/errno.h>).
#
# This directory goes LAST in every include list, and that placement is the
# whole trick: freewificam's compat headers forward with #include_next (for
# example components/net80211/ports/compat/sys/errno.h), which resumes the
# search after the directory the forwarding file was found in.  Listed after
# ports/compat, the shim is exactly what include_next then lands on; listed
# before it, include_next would miss it and the plain <sys/time.h> lookup would
# shadow a real libc header if one ever appears.
set(FWCAM_SHIM_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/port/shim")

# The USB world adds the board's CherryUSB shadow header (usb_config.h, ~40
# geometry keys resolved by include order), the board directory itself (its
# board_conf.h is what the glue includes first) and the imported stack's roots.
set(FWCAM_BOARD_INCLUDES
  "${FWCAM_PLATFORM}/board/rk3568-itx"
  "${FWCAM_PLATFORM}/board/rk3568-itx/drivers"
)

set(FWCAM_USB_INCLUDES
  ${FWCAM_BOARD_INCLUDES}
  "${FWCAM_PLATFORM}/board/rk3568-itx/usb"
  "${FWCAM_APP}/usb"
  "${FWCAM_USB}/common"
  "${FWCAM_USB}/core"
  "${FWCAM_USB}/class/hub"
  "${FWCAM_USB}/class/msc"
  "${FWCAM_USB}/class/vendor/wifi"
  "${FWCAM_USB}/port/ehci"
  "${FWCAM_USB}/port/xhci"
)

# --- per-world compile options --------------------------------------------

# The common tail.  -mgeneral-regs-only is the important one: this stack was
# written and board-tested with no floating point and no SIMD, Zephyr's arm64
# context switch only preserves them when CONFIG_FPU is on, and a stray vector
# register in a printf path would be a silent corruption - not a build error.
# -mstrict-align keeps the compiler from emitting unaligned multi-register
# accesses, which the board images also built with.
set(FWCAM_BASE_OPTS
  -O2
  -Wall
  -fno-stack-protector
  -march=armv8-a
  -mgeneral-regs-only
  -mstrict-align
  -mno-outline-atomics
  -ffunction-sections
  -fdata-sections
)

# The freestanding ISO-C11 worlds (platform seams and the vendor driver).
set(FWCAM_C11_OPTS ${FWCAM_BASE_OPTS} -std=c11 -ffreestanding -fno-builtin)
# The GNU worlds (linuxkpi seam, CherryUSB, the platform sources that use
# statement expressions).
set(FWCAM_GNU_OPTS ${FWCAM_BASE_OPTS} -std=gnu11)

# --- the libraries ---------------------------------------------------------

# 1. Platform: the console/log seam, the CMSIS-RTOS2 backend on Zephyr
#    primitives, the arch face (cache/GIC/counter/tick), the case table, the
#    runner and the two always-on cases.
zephyr_library_named(freewificam_platform)
zephyr_library_sources(
  "${FWCAM_PLATFORM}/os/baremetal/log.c"
  "${FWCAM_PLATFORM}/board/zephyr/uart.c"
  "${FWCAM_PLATFORM}/board/zephyr/platform.c"
  "${FWCAM_PLATFORM}/os/zephyr/cmsis2_zephyr.c"
  "${FWCAM_PLATFORM}/os/zephyr/cmsis2_ext.c"
  "${FREEWIFICAM_DIR}/third-party/ulog/src/ulog.c"
  "${FWCAM_APP}/main.c"
  "${FWCAM_APP}/runner.c"
  "${FWCAM_APP}/port/clock_board.c"
  "${FWCAM_APP}/cases/tick.c"
  "${CMAKE_CURRENT_LIST_DIR}/port/board_usb_zephyr.c"
)
zephyr_library_include_directories(
  ${FWCAM_COMMON_INCLUDES} ${FWCAM_BOARD_INCLUDES} ${FWCAM_SHIM_INCLUDES})
zephyr_library_compile_options(
  ${FWCAM_C11_OPTS} -Wextra ${FWCAM_LIBC_FORCE_INCLUDE} ${FWCAM_FORCE_INCLUDE})

# 2. USB host: the imported CherryUSB core, the hub class, both controller
#    ports the bench uses, its CMSIS-RTOS2 osal, and the board glue that owns
#    the RK3568 power/PHY/reset sequence.
zephyr_library_named(freewificam_usb)
zephyr_library_sources(
  "${FWCAM_USB}/core/usbh_core.c"
  "${FWCAM_USB}/class/hub/usbh_hub.c"
  "${FWCAM_USB}/port/ehci/usb_hc_ehci.c"
  "${FWCAM_USB}/port/xhci/usb_hc_xhci.c"
  "${FWCAM_USB}/ports/osal/cmsis2/osal_cmsis2.c"
  "${FWCAM_PLATFORM}/board/rk3568-itx/usb/usb_glue.c"
  "${FWCAM_PLATFORM}/board/rk3568-itx/usb/usb_domain.c"
)
zephyr_library_include_directories(${FWCAM_COMMON_INCLUDES} ${FWCAM_USB_INCLUDES} ${FWCAM_SHIM_INCLUDES})
zephyr_library_compile_options(
  ${FWCAM_GNU_OPTS} ${FWCAM_LIBC_FORCE_INCLUDE} ${FWCAM_FORCE_INCLUDE})

# 3. net80211: the NetBSD MLME core, its CMSIS-RTOS2 port, the bridge seam and
#    the crypto units.  GNU C, and its own forced-include prelude first - that
#    header decides which <sys/cdefs.h> wins, so it cannot come later.
zephyr_library_named(freewificam_net80211)
zephyr_library_sources(
  "${FWCAM_NET}/net80211/ieee80211.c"
  "${FWCAM_NET}/net80211/ieee80211_proto.c"
  "${FWCAM_NET}/net80211/ieee80211_node.c"
  "${FWCAM_NET}/net80211/ieee80211_output.c"
  "${FWCAM_NET}/net80211/ieee80211_input.c"
  "${FWCAM_NET}/net80211/ieee80211_amrr.c"
  "${FWCAM_NET}/net80211/ieee80211_netbsd.c"
  "${FWCAM_NET}/net80211/ieee80211_crypto.c"
  "${FWCAM_NET}/net80211/ieee80211_crypto_none.c"
  "${FWCAM_NET}/net80211/ieee80211_crypto_ccmp.c"
  "${FWCAM_NET}/crypto/aes/aes_bear.c"
  "${FWCAM_NET}/crypto/aes/aes_ct.c"
  "${FWCAM_NET}/crypto/aes/aes_ct_enc.c"
  "${FWCAM_NET}/crypto/aes/aes_ct_dec.c"
  "${FWCAM_NET}/crypto/aes/aes_ccm.c"
  "${FWCAM_NET}/crypto/aes/aes_ccm_mbuf.c"
  "${FWCAM_NET}/port/aes_impl_compat.c"
  "${FWCAM_NET}/ports/osal/cmsis2/osal_memory_target.c"
  "${FWCAM_NET}/ports/osal/cmsis2/osal_cmsis2.c"
  "${FWCAM_NET}/ports/net/mbuf.c"
  "${FWCAM_NET}/ports/net/ifnet.c"
  "${FWCAM_NET}/ports/bridge/wlan_bridge.c"
)
zephyr_library_include_directories(
  ${FWCAM_COMMON_INCLUDES}
  "${FREEWIFICAM_DIR}"
  "${FWCAM_NET}/ports/compat"
  "${FWCAM_NET}/compat/netbsd"
  "${FWCAM_NET}"
  "${FWCAM_NET}/ports"
  ${FWCAM_SHIM_INCLUDES}
)
zephyr_library_compile_options(
  ${FWCAM_GNU_OPTS}
  -Wno-unused-function
  -Wno-pointer-sign
  "-include${FWCAM_NET}/ports/compat/prelude.h"
  ${FWCAM_LIBC_FORCE_INCLUDE}
  "-include${FWCAM_LIBC_NAMES}"
  ${FWCAM_FORCE_INCLUDE}
)

# 4. linuxkpi: the Linux-shaped kernel API the driver compiles against, the USB
#    URB engine over CherryUSB, the seam adapter and the firmware blob.
zephyr_library_named(freewificam_linuxkpi)
zephyr_library_sources(
  "${FWCAM_LKPI}/rtos/linuxkpi_core.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_compat.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_platform_target.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_80211.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_80211_helpers.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_wiphy_work.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_txq.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_netdev.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_cfg80211.c"
  "${FWCAM_LKPI}/rtos/linuxkpi_osdep.c"
  "${FWCAM_LKPI}/rtos/lkpi_tcp_trace.c"
  "${FWCAM_LKPI}/rtos/fw_registry_rtos.c"
  "${FWCAM_LKPI}/rtos/skbuff/skbuff_pool.c"
  "${FWCAM_LKPI}/rtos/usb/linuxkpi_usb.c"
  "${FWCAM_LKPI}/rtos/usb/lkpi_usbh_class.c"
  "${FWCAM_LKPI}/rtos/usb/lkpi_usbh_modeswitch.c"
  "${FWCAM_LKPI}/rtos/usb/lkpi_usb_cherryusb.c"
  "${FWCAM_NET}/ports/bridge/wlan_seam_adapter.c"
  "${FWCAM_LKPI}/fw/rtl8188eufw_blob.c"
  "${FWCAM_APP}/cases/rtl8xxxu.c"
  "${FWCAM_APP}/cases/rtl8xxxu_scan.c"
  "${FWCAM_APP}/cases/rtl8xxxu_join.c"
  "${FWCAM_APP}/cases/rtl8xxxu_dataplane.c"
)
zephyr_library_include_directories(
  ${FWCAM_COMMON_INCLUDES}
  ${FWCAM_USB_INCLUDES}
  "${FREEWIFICAM_DIR}"
  "${FWCAM_LKPI}"
  "${FWCAM_LKPI}/rtos"
  "${FWCAM_LKPI}/rtos/include"
  "${FWCAM_LKPI}/rtos/usb"
  "${FWCAM_NET}"
  "${FWCAM_NET}/ports"
  "${FWCAM_NET}/ports/bridge"
  ${FWCAM_SHIM_INCLUDES}
)
zephyr_library_compile_options(
  ${FWCAM_GNU_OPTS}
  ${FWCAM_LIBC_FORCE_INCLUDE}
  "-include${FWCAM_LIBC_NAMES}"
  ${FWCAM_FORCE_INCLUDE}
)

# 5. The vendor driver.  Its own freestanding world, and no Kconfig define: the
#    chip's fileops table names every chip, so all nine units are in the link
#    closure whatever dongle is plugged in.
zephyr_library_named(freewificam_driver)
zephyr_library_sources(
  "${FWCAM_DRV}/core.c"
  "${FWCAM_DRV}/8188e.c"
  "${FWCAM_DRV}/8188f.c"
  "${FWCAM_DRV}/8192c.c"
  "${FWCAM_DRV}/8192e.c"
  "${FWCAM_DRV}/8192f.c"
  "${FWCAM_DRV}/8710b.c"
  "${FWCAM_DRV}/8723a.c"
  "${FWCAM_DRV}/8723b.c"
)
zephyr_library_include_directories(
  ${FWCAM_COMMON_INCLUDES}
  "${FREEWIFICAM_DIR}"
  "${FWCAM_LKPI}/rtos"
  "${FWCAM_LKPI}/rtos/include"
  "${FWCAM_DRV}"
  ${FWCAM_SHIM_INCLUDES}
)
zephyr_library_compile_options(
  ${FWCAM_C11_OPTS}
  -Wno-unused-parameter
  ${FWCAM_LIBC_FORCE_INCLUDE}
  ${FWCAM_FORCE_INCLUDE}
)

# --- gates ----------------------------------------------------------------

# The registry walks are the failure mode this checks for: with --gc-sections a
# table nothing references by address is dropped, the image enumerates the
# device and then claims no class.  The linker fragment KEEPs them; this warns
# at configure time if the fragment is ever lost.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/port/freewificam_sections.ld")
  message(FATAL_ERROR "the freewificam linker fragment is missing")
endif()
