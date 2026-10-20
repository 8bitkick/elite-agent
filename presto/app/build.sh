#!/bin/bash
# presto/app/build.sh [--efficient] [--autostart=FRAMES] [--disc=FILE] [--out=DIR]: the agent as an
# app of rp2350-bbc-micro's (its src/common/app_api.h), built on its own: no Pico SDK, linked at
# the app slot (app.ld), with newlib's static libc and libm for the string functions and the
# maths and app_libc.c for the rest. Writes <out>/elite-agent.bin, the app slot's image (pack.py):
# its header says what it is (name, version, machine, size, CRC). The running firmware installs
# it: the web panel's Application section, POST /api/app, or rp2350-bbc-micro's
#   tools/presto.py app install elite-agent.bin
#   API_DIR        where app_api.h and machine_hook.h are (default: presto/api, this repository's copies)
#   PICO_SDK_PATH  the Pico SDK, for its maths only: its double and float routines (the RP2350's
#                  double coprocessor and the FPU), compiled into the app as the firmware has them
#                  (--wrap); nothing of the SDK is called. Without it: libgcc's and newlib's,
#                  six times slower (PLAYER time ~800 us a frame, not ~130)
# The disc (default: none) goes in the image: the firmware boots it as its built-in disc, its
# saved games in the save disc. Curious mode unless
# --efficient; the agent starts by itself ELITE_AUTOSTART frames after boot (2500).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; TOP="$(cd "$HERE/../.." && pwd)"
API_DIR=${API_DIR:-$TOP/presto/api}; OUT=$HERE/build
CURIOUS=1; AUTO=2500; DISC=
for a in "$@"; do case $a in --efficient) CURIOUS=0;; --autostart=*) AUTO=${a#*=};; --disc=*) DISC=${a#*=};;
    --out=*) OUT=${a#*=};; *) echo "unknown: $a"; exit 1;; esac; done
API="$API_DIR/app_api.h"
[ -f "$API" ] || { echo "no $API (API_DIR: app_api.h's directory)"; exit 1; }
mkdir -p "$OUT"
VERSION=${ELITE_VERSION:-$(git -C "$TOP" describe --always --dirty 2>/dev/null || echo dev)}
# the slot and the RAM area, from app_api.h (app.ld checks it agrees)
OFF=$(sed -n 's/^#define APP_SLOT_OFFSET \(0x[0-9a-f]*\)u.*/\1/p' "$API")
KB=$(sed -n 's/^#define APP_RAM_SIZE (\([0-9]*\) \* 1024).*/\1/p' "$API")
[ -n "$OFF" ] && [ -n "$KB" ] || { echo "$API: no APP_SLOT_OFFSET / APP_RAM_SIZE"; exit 1; }
CPU="-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp"
CFLAGS="$CPU -O2 -fwrapv -g -ffunction-sections -fdata-sections -fno-common -Wall -Wno-unused-function
  -DAPP_LOADED=1 -DPLAYER_CURIOUS=$CURIOUS -DELITE_AUTOSTART=$AUTO -DELITE_VERSION=\"$VERSION\"
  -DELITE_DISC=\"$(basename "${DISC:-}")\" -include $HERE/app_libc.h -I$TOP/cplayer -I$API_DIR"
OBJS=
for f in "$TOP"/presto/elite_presto.c "$TOP"/presto/elite_splash.c "$HERE"/app_libc.c \
         "$TOP"/cplayer/{keyq,agent,cfly,combat,docked,drv,elite_ver,fair,flight,galaxy,pilot,ships,show}.c; do
    o="$OUT/$(basename "${f%.c}").o"
    [ "$o" -nt "$f" ] && [ "$o" -nt "$API" ] && [ "$o" -nt "$0" ] || arm-none-eabi-gcc $CFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
# the SDK's maths: compiled with its headers (a stub for the two a configured build would make)
WRAP=
if [ -n "$PICO_SDK_PATH" ]; then
    S=$PICO_SDK_PATH/src; mkdir -p "$OUT/gen/pico"
    printf '#define PICO_RP2350 1\n#define PICO_ON_DEVICE 1\n#define PICO_NO_HARDWARE 0\n' > "$OUT/gen/pico/config_autogen.h"
    printf '#define PICO_SDK_VERSION_STRING "app"\n' > "$OUT/gen/pico/version.h"
    SDKINC="-I$OUT/gen"
    for d in common/pico_base_headers rp2350/pico_platform rp2350/hardware_regs rp2_common/hardware_base \
             rp2_common/pico_platform_compiler rp2_common/pico_platform_sections rp2_common/pico_platform_panic \
             rp2_common/pico_platform_common rp2_common/hardware_dcp rp2_common/hardware_rcp rp2_common/pico_double \
             rp2_common/pico_float rp2350/hardware_structs rp2_common/pico_bootrom rp2350/boot_stage2 \
             common/boot_picobin_headers common/boot_picoboot_headers; do SDKINC="$SDKINC -I$S/$d/include"; done
    SDKINC="$SDKINC -I$S/boards/include"
    for f in pico_double/double_aeabi_dcp.S pico_double/double_conv_m33.S pico_double/double_sci_m33.S \
             pico_double/double_fma_dcp.S pico_double/double_math.c pico_float/float_sci_m33_vfp.S \
             pico_float/float_conv32_vfp.S pico_float/float_common_m33.S pico_float/float_math.c; do
        o="$OUT/sdk_$(basename "$f").o"
        [ "$o" -nt "$S/rp2_common/$f" ] || arm-none-eabi-gcc $CPU -O2 -ffunction-sections -DLIB_PICO_DOUBLE_PICO=1 \
            -DLIB_PICO_FLOAT_PICO=1 -DLIB_PICO_FLOAT_PICO_VFP=1 $SDKINC -c "$S/rp2_common/$f" -o "$o"
        OBJS="$OBJS $o"
    done
    for w in $(arm-none-eabi-nm "$OUT"/sdk_*.o | sed -n 's/.* T __wrap_//p' | sort -u); do WRAP="$WRAP -Wl,--wrap=$w"; done
else
    echo "no PICO_SDK_PATH: libgcc's and newlib's maths, much slower"
fi
lib() { arm-none-eabi-gcc $CPU -print-file-name=$1; }
arm-none-eabi-gcc $CPU -nostdlib -nostartfiles -T "$HERE/app.ld" -Wl,--gc-sections -Wl,-Map="$OUT/elite-agent.map" $WRAP \
    -Wl,--defsym=APP_SLOT_XIP=$((0x10000000 + OFF)) -Wl,--defsym=APP_RAM_BASE=$((0x20080000 - KB * 1024)) \
    -Wl,--defsym=APP_RAM_SIZE=$((KB * 1024)) -o "$OUT/elite-agent.elf" $OBJS \
    "$(lib libm.a)" "$(lib libc.a)" -lgcc
arm-none-eabi-size -A "$OUT/elite-agent.elf" | grep -E "^\.(text|data|bss|ARM)"
arm-none-eabi-objcopy -O binary "$OUT/elite-agent.elf" "$OUT/elite-agent.elf.bin"
python3 "$HERE/pack.py" "$OUT/elite-agent.elf.bin" "${DISC:--}" "$OUT/elite-agent.bin"
