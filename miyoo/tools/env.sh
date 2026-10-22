# Local settings of the PC-side scripts, sourced by them (REPO must be set). Set them in the
# environment or in miyoo/env.local.sh (not in git), for example:
#   MIYOO_HOST=192.168.1.50                    # the Miyoo's address (SMB shares Roms, Saves)
#   MSSH=/path/to/mssh                         # runs a command on the Miyoo over SSH
#   TC=/path/to/arm-gnu-toolchain-13.3.rel1-<host>-arm-none-linux-gnueabihf/bin
# Without TC the toolchain is looked up in PATH.
[ -f "$REPO/miyoo/env.local.sh" ] && . "$REPO/miyoo/env.local.sh"
MSSH=${MSSH:-mssh}
if [ -z "$TC" ]; then
	TC=$(command -v arm-none-linux-gnueabihf-gcc 2>/dev/null) && TC=$(dirname "$TC")
fi
