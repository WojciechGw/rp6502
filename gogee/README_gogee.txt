1. github WojciechGw/rp6502 -> sync fork
2. clone https://github.com/WojciechGw/rp6502.git into c:\@prg\@picocomputer
3. cd c:\@prg\@picocomputer\rp6502
4. git submodule update --init
5. Raspberry Pi Pico Project -> import project
6. cmake -G Ninja -DCMAKE_MAKE_PROGRAM="C:/users/wojciech/.pico-sdk/ninja/v1.12.1/ninja.exe" -S . -B build

src/CMakeLists.txt

# to disables all cyw43 debug infos
add_compile_definitions(
  PICO_CYW43_LOGGING_ENABLED=0        # SDK ≥ 2.0 … 2.1+ (disables all cyw43 printf)
  PICO_CYW43_ARCH_DEBUG_ENABLED=0     # SDK 1.x fallback
  LWIP_DEBUG=0                        # silence lwIP
)

set_property(TARGET rp6502_ria rp6502_vga
    APPEND PROPERTY COMPILE_DEFINITIONS
    RP6502_NAME="Picocomputer 6502"
    RP6502_VERSION="gogee"
    RP6502_CODE_PAGE=0
    RP6502_KEYBOARD=PL
    RP6502_EXFAT=0
)