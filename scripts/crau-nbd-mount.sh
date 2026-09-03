#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# crau-nbd-mount: FreeDesktop launcher helper for crau-nbd

set -e

PROGRAM_NAME="crau-nbd-mount"
CRAU_NBD_BIN="crau-nbd"

if ! command -v "$CRAU_NBD_BIN" >/dev/null 2>&1; then
    DIR="$(cd "$(dirname "$0")" && pwd)"
    if [ -x "$DIR/../build/crau-nbd" ]; then
        CRAU_NBD_BIN="$DIR/../build/crau-nbd"
    elif [ -x "$DIR/crau-nbd" ]; then
        CRAU_NBD_BIN="$DIR/crau-nbd"
    elif [ -x "/usr/local/bin/crau-nbd" ]; then
        CRAU_NBD_BIN="/usr/local/bin/crau-nbd"
    fi
fi

notify() {
    TITLE="$1"
    MSG="$2"
    if command -v notify-send >/dev/null 2>&1; then
        notify-send -i drive-harddisk "$TITLE" "$MSG" || true
    else
        printf "%s: %s: %s
" "$PROGRAM_NAME" "$TITLE" "$MSG" >&2
    fi
}

error_dialog() {
    MSG="$1"
    if [ -n "$DISPLAY" ] || [ -n "$WAYLAND_DISPLAY" ]; then
        if command -v zenity >/dev/null 2>&1; then
            zenity --error --title="CrAU-NBD Error" --text="$MSG" --width=450 || true
            return
        fi
    fi
    notify "Error" "$MSG"
    printf "%s: error: %s
" "$PROGRAM_NAME" "$MSG" >&2
}

detach_all() {
    FOUND=0
    USER_NAME="${USER:-$(id -un)}"
    USER_ID="$(id -u)"

    # 1. Unmount and detach active crau-nbd mount points
    for MNT in "/run/media/$USER_NAME"/crau-nbd-* /tmp/crau-nbd-*; do
        if [ -d "$MNT" ]; then
            DEV="$(findmnt -n -o SOURCE "$MNT" 2>/dev/null || true)"
            if [ -n "$DEV" ]; then
                FOUND=1
                pkexec umount "$MNT" 2>/dev/null || true
                rmdir "$MNT" 2>/dev/null || true
                if [ -b "$DEV" ]; then
                    pkexec "$CRAU_NBD_BIN" detach -d "$DEV" 2>/dev/null || true
                fi
                notify "CrAU-NBD Detached" "Unmounted $MNT ($DEV)"
            fi
        fi
    done

    # 2. Unmount any active mount-zip auxiliary mounts
    for ZMNT in "/run/user/$USER_ID"/crau-nbd-zip-* /tmp/crau-nbd-zip-*; do
        if [ -d "$ZMNT" ]; then
            fusermount -u "$ZMNT" 2>/dev/null || umount "$ZMNT" 2>/dev/null || true
            rmdir "$ZMNT" 2>/dev/null || true
        fi
    done

    if [ "$FOUND" -eq 0 ]; then
        notify "CrAU-NBD" "No active CrAU-NBD mounts found."
    fi
}

if [ "$1" = "--detach" ] || [ "$1" = "-u" ]; then
    detach_all
    exit 0
fi

if [ -z "$1" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    printf "Usage: %s [OPTION] FILE
" "$PROGRAM_NAME"
    printf "Open and mount Android/ChromeOS OTA payload (.zip or .bin) via CrAU-NBD.

"
    printf "Options:
"
    printf "  -u, --detach          unmount and detach active CrAU-NBD devices
"
    printf "  -h, --help            display this help and exit
"
    exit 0
fi

TARGET_FILE="$1"
if [ ! -f "$TARGET_FILE" ]; then
    error_dialog "File not found: $TARGET_FILE"
    exit 1
fi

if [ ! -e /dev/nbd0 ]; then
    pkexec modprobe nbd || {
        error_dialog "Failed to load 'nbd' kernel module."
        exit 1
    }
fi

ZIP_FUSE_MOUNT=""

# Test listing partitions from target
LIST_OUTPUT=""
if ! LIST_OUTPUT="$("$CRAU_NBD_BIN" list "$TARGET_FILE" 2>&1)"; then
    case "$LIST_OUTPUT" in
        *"compressed (method"*"!= STORED)"*)
            # Deflated zip detected: check if mount-zip is available
            if command -v mount-zip >/dev/null 2>&1; then
                notify "CrAU-NBD" "Deflated ZIP detected. Mounting container with mount-zip..."
                USER_ID="$(id -u)"
                ZIP_FUSE_MOUNT="/run/user/$USER_ID/crau-nbd-zip-$$"
                mkdir -p "$ZIP_FUSE_MOUNT"
                if ! mount-zip "$TARGET_FILE" "$ZIP_FUSE_MOUNT"; then
                    rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
                    error_dialog "Failed to mount ZIP container with mount-zip."
                    exit 1
                fi

                VIRT_PAYLOAD="$ZIP_FUSE_MOUNT/payload.bin"
                if [ ! -f "$VIRT_PAYLOAD" ]; then
                    fusermount -u "$ZIP_FUSE_MOUNT" 2>/dev/null || true
                    rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
                    error_dialog "No 'payload.bin' found inside the mounted ZIP container."
                    exit 1
                fi
                TARGET_FILE="$VIRT_PAYLOAD"
                if ! LIST_OUTPUT="$("$CRAU_NBD_BIN" list "$TARGET_FILE" 2>&1)"; then
                    fusermount -u "$ZIP_FUSE_MOUNT" 2>/dev/null || true
                    rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
                    error_dialog "Failed to inspect payload.bin inside mounted zip:
$LIST_OUTPUT"
                    exit 1
                fi
            else
                error_dialog "The 'payload.bin' inside this ZIP archive is compressed (deflated).

Deflated ZIP archives do not support random block access.

Please install 'mount-zip' (optional dependency) to mount deflated archives automatically:
    pacman -S mount-zip  (or build from AUR)

Alternatively, extract payload.bin first."
                exit 1
            fi
            ;;
        *)
            error_dialog "$LIST_OUTPUT"
            exit 1
            ;;
    esac
fi

NBD_DEV=""
for i in $(seq 0 15); do
    CANDIDATE="/dev/nbd$i"
    if [ -b "$CANDIDATE" ]; then
        SZ="$(lsblk -n -b -o SIZE "$CANDIDATE" 2>/dev/null || echo "1")"
        if [ "$SZ" = "0" ] || [ -z "$SZ" ]; then
            NBD_DEV="$CANDIDATE"
            break
        fi
    fi
done

if [ -z "$NBD_DEV" ]; then
    [ -n "$ZIP_FUSE_MOUNT" ] && fusermount -u "$ZIP_FUSE_MOUNT" 2>/dev/null && rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
    error_dialog "No free NBD block devices available (/dev/nbd0 - /dev/nbd15 are all in use)."
    exit 1
fi

PART_ROWS=""
SELECTED_PART=""

TMP_LIST="$(mktemp)"
printf '%s
' "$LIST_OUTPUT" > "$TMP_LIST"
while IFS= read -r line; do
    case "$line" in
        "  "*)
            PNAME="$(echo "$line" | awk '{print $1}')"
            PSIZE="$(echo "$line" | sed -n 's/.*(\(.*\))//p')"
            if [ -n "$PNAME" ]; then
                DEFAULT="FALSE"
                [ "$PNAME" = "system" ] && DEFAULT="TRUE"
                PART_ROWS="$PART_ROWS $DEFAULT $PNAME "$PSIZE""
            fi
            ;;
    esac
done < "$TMP_LIST"
rm -f "$TMP_LIST"

if [ -n "$DISPLAY" ] || [ -n "$WAYLAND_DISPLAY" ]; then
    if command -v zenity >/dev/null 2>&1 && [ -n "$PART_ROWS" ]; then
        CMD="zenity --list --radiolist --title="CrAU-NBD: Select Partition" --text="Choose a partition to mount from $(basename "$TARGET_FILE"):" --column="Select" --column="Partition" --column="Size" $PART_ROWS --width=450 --height=320"
        SELECTED_PART="$(eval "$CMD" 2>/dev/null || true)"
        if [ -z "$SELECTED_PART" ]; then
            [ -n "$ZIP_FUSE_MOUNT" ] && fusermount -u "$ZIP_FUSE_MOUNT" 2>/dev/null && rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
            exit 0
        fi
    fi
fi

if [ -z "$SELECTED_PART" ]; then
    if echo "$LIST_OUTPUT" | grep -q "  system "; then
        SELECTED_PART="system"
    else
        SELECTED_PART="$(echo "$LIST_OUTPUT" | awk '/^  [a-zA-Z0-9_-]+/ {print $1; exit}')"
    fi
fi

if [ -z "$SELECTED_PART" ]; then
    [ -n "$ZIP_FUSE_MOUNT" ] && fusermount -u "$ZIP_FUSE_MOUNT" 2>/dev/null && rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
    error_dialog "Could not find any mountable partitions in $TARGET_FILE."
    exit 1
fi

USER_NAME="${USER:-$(id -un)}"
MOUNT_DIR="/run/media/$USER_NAME/crau-nbd-$SELECTED_PART"
[ -d "/run/media/$USER_NAME" ] || MOUNT_DIR="/tmp/crau-nbd-$USER_NAME-$SELECTED_PART"

pkexec mkdir -p "$MOUNT_DIR"

LOG_FILE="/tmp/crau-nbd-$(basename "$NBD_DEV").log"
pkexec sh -c "$CRAU_NBD_BIN attach -d '$NBD_DEV' -p '$SELECTED_PART' '$TARGET_FILE' > '$LOG_FILE' 2>&1 &"

READY=0
for _ in $(seq 1 50); do
    SZ="$(lsblk -n -b -o SIZE "$NBD_DEV" 2>/dev/null || echo "0")"
    if [ "$SZ" -gt 0 ] 2>/dev/null; then
        READY=1
        break
    fi
    sleep 0.1
done

if [ "$READY" -ne 1 ]; then
    ERR_LOG="$(cat "$LOG_FILE" 2>/dev/null || echo "Unknown error")"
    [ -n "$ZIP_FUSE_MOUNT" ] && fusermount -u "$ZIP_FUSE_MOUNT" 2>/dev/null && rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
    error_dialog "Failed to attach $SELECTED_PART to $NBD_DEV:
$ERR_LOG"
    exit 1
fi

if ! pkexec mount -o ro "$NBD_DEV" "$MOUNT_DIR"; then
    pkexec "$CRAU_NBD_BIN" detach -d "$NBD_DEV" 2>/dev/null || true
    [ -n "$ZIP_FUSE_MOUNT" ] && fusermount -u "$ZIP_FUSE_MOUNT" 2>/dev/null && rmdir "$ZIP_FUSE_MOUNT" 2>/dev/null || true
    error_dialog "Failed to mount $NBD_DEV to $MOUNT_DIR."
    exit 1
fi

notify "CrAU-NBD Mounted" "Partition '$SELECTED_PART' is mounted at:
$MOUNT_DIR

Run 'crau-nbd-mount --detach' to unmount."

if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$MOUNT_DIR" >/dev/null 2>&1 &
fi

exit 0
