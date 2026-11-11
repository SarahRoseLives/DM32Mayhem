#!/bin/sh
# SPDX-License-Identifier: MIT
# Build the standalone packet-BBS firmware for the DM32. Everything not needed to run a 1200 baud AX.25 BBS on FM is
# turned off, so the connected-mode stack, the store and the sessions fit the radio's RAM.
#
# CONFIG_DMR stays on: the HR_C7000 "DMR glue" (its system interrupt and FM RX hook) is what the AFSK/APRS receive
# path needs. Its voice and data features are all off, so no DMR codec is needed.
#
#   tools/build-bbs.sh            build build/bbs/rfoundry_dm32.bin
#   tools/build-bbs.sh -j1        pass make options through
set -e
here=$(cd "$(dirname "$0")" && pwd)
cd "$here/../firmware"
export PATH="$HOME/toolchains/csky-elf/bin:$PATH"

exec make BOARD=dm32 BUILD=build/bbs -j"$(nproc)" \
	CONFIG_BBS=y CONFIG_SAME_TX=y CONFIG_OOK_TX=y CONFIG_MDC_TX=y CONFIG_P25CC_TX=y CONFIG_APRS_TX=y CONFIG_RTTY_TX=y CONFIG_BOOT_GATE=y CONFIG_NO_PTT=y \
	CONFIG_DMR=y CONFIG_DMR_DATA_CORE=n CONFIG_DMR_IP_GATEWAY=n CONFIG_DMR_PRIVACY=n \
	CONFIG_DMR_UDP_COMPRESS=n CONFIG_DMR_CONFIRMED=n CONFIG_CRASH_REPORT=n \
	CONFIG_DMR_TALKER_ALIAS=n CONFIG_DMR_GPS=n CONFIG_DMR_SMS=n CONFIG_DMR_TMS=n CONFIG_DMR_ARS=n \
	CONFIG_DMR_LRRP=n CONFIG_DMR_SIGNALING=n CONFIG_DMR_EMERGENCY=n CONFIG_DMR_ROAMING=n \
	CONFIG_HOTSPOT=n CONFIG_SNMP=n CONFIG_SFR=n CONFIG_ZONE_SHARE=n \
	CONFIG_NET_TCP=n CONFIG_NET_HTTP=n CONFIG_NET_WEBCTL=n CONFIG_NET_RIGCTL=n \
	CONFIG_NET_APRSIS=n CONFIG_NET_USRP=n CONFIG_MDNS=n CONFIG_TFTP=n CONFIG_IDDB_COMPACT=n \
	CONFIG_DMR_CONTROL_STATION=n CONFIG_NET_PPP=n CONFIG_USB_NETWORK=n \
	CONFIG_APRS_IGATE=n CONFIG_GPS_SKY=n CONFIG_VFO_DMR=n CONFIG_CONTACT_EDIT=n \
	CONFIG_FONT_XL=n CONFIG_FONT_AA=n CONFIG_FONT_BIG=n CONFIG_LANG_ES=n \
	"$@"
