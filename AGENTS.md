# AGENTS.md — CrAU-NBD Project Context & Agent Operational Manual

## 1. Project Overview & Mission

**`crau-nbd`** is a high-performance, standalone Linux Network Block Device (NBD) daemon designed to map, stream, and mount individual partition images (`system`, `vendor`, `product`, `ROOT-A`, `kernel`, etc.) inside ChromeOS and Android **CrAU v2 `payload.bin`** files with **zero intermediate disk extraction**.

### Key Architectural Pillars
1. **Zero-Extraction Block Streaming**: Mounts and streams uncompressed partition blocks directly from raw `.bin` payloads or uncompressed (`Stored`) OTA `.zip` containers using 64-bit POSIX `pread()` block arithmetic without writing gigabytes of scratch images to disk.
2. **Deflate FUSE Interoperability**: Strictly rejects deflated outer ZIP archives (which do not support $O(1)$ random seeking) and cleanly delegates to [**`google/mount-zip`**](https://github.com/google/mount-zip) to expose virtual, seekable payload streams.
3. **In-Process Linux Kernel Bridge**: Bypasses network sockets entirely by bridging the Linux in-kernel NBD driver (`/dev/nbd*`) directly to userspace via `socketpair(AF_UNIX, SOCK_STREAM)` and `<linux/nbd.h>` ioctl interfaces.
4. **Strict Standards Conformance**:
   - **IEEE Std 1003.1 (POSIX.1)**: Strict adherence to POSIX Utility Syntax Guidelines 3–11, thread-safe asynchronous I/O, `socketpair` IPC, and `sigaction` signal handling with `volatile sig_atomic_t`.
   - **GNU Coding Standards**: GNU error reporting format (`program: error: message` per § 4.9), canonical `--version` (§ 4.16.1) and `--help` (§ 4.16.2), standard GNU packaging files (`AUTHORS`, `NEWS`, `ChangeLog`, `INSTALL`, `README`, `COPYING`), and `GNUInstallDirs`.
5. **Supervisorable Privilege Elevation**: Integrates natively with **PolicyKit (`pkexec`)** via `data/polkit/org.blissos.craunbd.policy` for transparent GUI authentication dialogs and `journald` auditability.
6. **FreeDesktop (XDG) Ecosystem Integration**: Desktop entry (`org.blissos.craunbd.desktop`), Shared MIME-info database (`org.blissos.craunbd.xml`), and the `crau-nbd-mount` launcher wrapper providing GUI partition selection via `zenity` and desktop alerts via `notify-send`.
7. **Dual-Libc Support**: Compiles cleanly and runs natively on both **glibc** (dynamic) and **musl libc** (100% statically linked standalone binary).

---

## 2. Architectural Design Patterns & Standards

### Design Pattern Reference Matrix

| Layer / Component | Design Pattern | Implementation | Description |
|---|---|---|---|
| **Kernel Block Driver Bridge** | **In-Process Kernel Driver** | `NbdServer`, `<linux/nbd.h>` | Bypasses network sockets; connects userspace block engine to `/dev/nbd*` over `socketpair(AF_UNIX)`. |
| **Block Search & Translation** | **Interval Search Tree** | `ExtentIndex`, `std::upper_bound` | Maps logical partition block addresses to `InstallOperation` extents in $O(\log N)$ time. |
| **Operation Caching** | **Thread-Safe LRU Cache** | `BlockCache` (`std::list` + `std::unordered_map`) | Caches decompressed multi-block operations (default: 128 MB) to eliminate redundant decompression passes. |
| **Multi-Algorithm Decompressor** | **Strategy & Dispatcher** | `Decompressor` (`liblzma`, `libbz2`) | Dispatches streaming block decoding for `REPLACE`, `REPLACE_XZ`, `REPLACE_BZ`, and `ZERO`. |
| **Zero-Copy Container Seeker** | **Source Provider Abstraction** | `ISourceReader`, `FileSourceReader`, `ZipEntrySourceReader` | Reads raw payloads or calculates byte offsets of uncompressed entries inside OTA ZIP containers. |
| **Error & Diagnostic Subsystem** | **GNU Diagnostic Pipeline** | `src/error.cpp`, `gnu_error()`, `gnu_posix_error()` | Formats errors strictly as `<program>: error: <msg>` without capitalization or trailing periods (§ 4.9). |
| **Privilege Separation** | **PolicyKit Supervisor** | `org.blissos.craunbd.policy`, `pkexec` | Authorizes hardware device manipulation (`ioctl`) with desktop dialogs and system audit trails. |
| **Desktop Integration** | **FreeDesktop (XDG) Shell Wrapper** | `crau-nbd-mount`, `org.blissos.craunbd.desktop` | Right-click context menu mounter with automated Zenity partition picker and `xdg-open` file manager launch. |

---

## 3. Directory Layout

```
crau-nbd/
├── AGENTS.md                  # Agent Operational Manual & Architectural Context
├── AUTHORS                    # Package Authors (GNU § 6.1)
├── ChangeLog                  # Project Change Log (GNU § 6.2)
├── CMakeLists.txt             # Root CMake Build Configuration (C++20, GNUInstallDirs)
├── CODE_OF_CONDUCT.md         # Contributor Covenant v2.1
├── CONTRIBUTING.md            # Guidelines for Contributing & Code Style
├── COPYING                    # GNU General Public License v3.0 (GPL-3.0-or-later)
├── INSTALL                    # Generic GNU Installation Instructions
├── Makefile                   # GNU Top-Level Makefile Wrapper
├── NEWS                       # User-Visible Feature History (GNU § 6.3)
├── README                     # Plain-Text Package Overview
├── README.md                  # Markdown User Guide, Architecture & CLI Manual
├── data/
│   ├── desktop/
│   │   └── org.blissos.craunbd.desktop   # FreeDesktop v1.5 Desktop Entry ("Open with CrAU-NBD")
│   ├── mime/
│   │   └── org.blissos.craunbd.xml       # Shared MIME Info for application/x-crau-payload
│   └── polkit/
│       └── org.blissos.craunbd.policy    # PolicyKit Action Rules for pkexec
├── doc/
│   └── crau-nbd.1             # Section 1 Troff Man Page
├── .github/
│   └── workflows/
│       └── build.yml          # GitHub Actions CI (glibc Ubuntu + musl-static Alpine 3.18)
├── proto/
│   └── update_metadata.proto  # AOSP / ChromeOS DeltaArchiveManifest Protobuf Definition
├── scripts/
│   └── crau-nbd-mount.sh      # Portable XDG Desktop Launcher Script (crau-nbd-mount)
├── src/
│   ├── block_cache.cpp        # LRU Block Cache Implementation
│   ├── block_cache.hpp        # Thread-safe LRU Cache Header
│   ├── crau_parser.cpp        # CrAU v2 Header & Manifest Protobuf Parser
│   ├── crau_parser.hpp        # Parser & Partition Metadata Definitions
│   ├── decompressor.cpp       # liblzma (XZ) and libbz2 (bzip2) Decompressors
│   ├── decompressor.hpp       # Decompressor Engine Header
│   ├── error.cpp              # GNU Diagnostic Formatting & Reporting Implementation
│   ├── error.hpp              # GNU Diagnostic Headers & Program Name Storage
│   ├── extent_index.cpp       # O(log N) Extent Interval Tree Implementation
│   ├── extent_index.hpp       # Extent Interval Search Header
│   ├── main.cpp               # POSIX getopt_long CLI Entry Point & Signal Handlers
│   ├── nbd_server.cpp         # Linux Kernel <linux/nbd.h> In-Process Server Loop
│   ├── nbd_server.hpp         # NBD Server & Device Detach Header
│   ├── source_provider.cpp    # Raw File & Stored ZIP Byte Seeker Implementation
│   └── source_provider.hpp    # ISourceReader Interface & ZIP Central Directory Parser
└── tests/
    └── test_reader.cpp        # Automated Test Suite (ELF magic & Extent Verification)
```

---

## 4. Engine Lifecycle & Architecture

### State & Request Flow
```
Payload / OTA ZIP (Stored)
           │
           ▼
     open_source() ──[Deflated ZIP]──> Exit with Code 1 (Direct to mount-zip)
           │
     [Raw / Stored]
           ▼
      CrauParser ──> Validate Magic "CrAU", Major Version 2
           │
           ▼
  DeltaArchiveManifest (Deserialized from Protobuf)
           │
           ▼
      ExtentIndex ──> Sorted std::vector<ExtentInterval>
           │
           ▼
       NbdServer
           ├── socketpair(AF_UNIX) ──> sv[0] passed to kernel via ioctl(NBD_SET_SOCK)
           ├── Background Thread  ──> ioctl(nbd_fd, NBD_DO_IT)
           └── Main Request Loop  ──> Reads struct nbd_request from sv[1]
                                          │
                                          ├─[NBD_CMD_READ]──> ExtentIndex::lookup(LBA)
                                          │                      │
                                          │                      ▼
                                          │                 BlockCache (Hit / Miss)
                                          │                      │
                                          │                      ▼
                                          │                 Decompressor (LZMA/BZ2/Raw)
                                          │                      │
                                          │                      ▼
                                          │                 Send struct nbd_reply + Data
                                          │
                                          ├─[NBD_CMD_WRITE]─> Reply error EROFS
                                          │
                                          └─[NBD_CMD_DISC] ─> Clean exit & ioctl(NBD_DISCONNECT)
```

### In-Process NBD Protocol Details
* **Device Setup**:
  - Device opened with `O_RDWR | O_CLOEXEC`.
  - Sockets created via `socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv)`.
  - Block size set to partition block size (typically 4096 bytes) via `ioctl(nbd_fd, NBD_SET_BLKSIZE, 4096)`.
  - Partition size set in blocks via `ioctl(nbd_fd, NBD_SET_SIZE_BLOCKS, total_blocks)`.
  - Read-only flag asserted via `ioctl(nbd_fd, NBD_SET_FLAGS, NBD_FLAG_READ_ONLY | NBD_FLAG_HAS_FLAGS)`.
* **Request Handling**:
  - Magic verified against `NBD_REQUEST_MAGIC` (`0x25609513`).
  - Read commands unpack `be64toh(req.from)` and `ntohl(req.len)`.
  - Replies return `NBD_REPLY_MAGIC` (`0x67446698`), `error = 0`, matching 8-byte `req.handle`, followed immediately by decompressed bytes.

---

## 5. Build, Test, and Packaging Quick Reference

### Native Dynamic Build (glibc)
```bash
# Configure with GNU standards
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Compile all targets
cmake --build build -j$(nproc)

# Run automated tests against local test block
ctest --test-dir build --output-on-failure
```

### Static Standalone Build (musl-static via Alpine 3.18)
```bash
# Uses pre-built static libraries (libprotobuf.a, libbz2.a, liblzma.a) in alpine:3.18
cmake -B build-static -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON -G Ninja
cmake --build build-static
strip build-static/crau-nbd
```

### Running Supervised Mounting (pkexec)
```bash
# 1. Inspect partitions inside an OTA zip
./build/crau-nbd -v list <archive.zip | payload.bin>

# 2. Attach partition (e.g. system) to /dev/nbd0
pkexec ./build/crau-nbd -v attach -d /dev/nbd0 -p system <archive.zip | payload.bin> &

# 3. Mount read-only
pkexec mount -o ro /dev/nbd0 /mnt/target

# 4. Detach and release device
pkexec umount /mnt/target
pkexec ./build/crau-nbd detach -d /dev/nbd0
```

### FreeDesktop Launcher Wrapper
```bash
# Mount any payload or OTA zip interactively with Zenity and file manager launch
crau-nbd-mount <archive.zip | payload.bin>

# Detach all active mounts
crau-nbd-mount --detach
```

### Arch Linux AUR Package (`~/Documents/aur/crau-nbd`)
```bash
cd ~/Documents/aur/crau-nbd
updpkgsums
makepkg --printsrcinfo > .SRCINFO
makepkg -f
```

---

## 6. Guidelines for AI Agents Modifying This Codebase

1. **Maintain Zero-Warning Clean Builds**: Always compile with `-Wall -Wextra -Wpedantic -Wformat=2`. Do not introduce compiler or linker warnings under GCC or Clang.
2. **Preserve IEEE Std 1003.1 (POSIX.1) Compliance**:
   - Use POSIX-compliant file I/O (`pread`, `pwrite`, `open` with `O_CLOEXEC`).
   - Implement `EINTR` retry loops on all socket and file descriptor read/write calls (`posix_read_full`, `posix_write_all`).
   - Handle signals cleanly via `sigaction` with atomic `volatile sig_atomic_t` flags.
3. **Preserve GNU Coding Standards (§ 4.9, § 4.16)**:
   - Diagnostic messages must use `gnu_error()` or `gnu_posix_error()` printing `<program>: error: <message>` to `stderr`. Never capitalize the first word and never end with a trailing period.
   - `--help` and `--version` must strictly conform to GNU standards templates.
4. **Preserve Deflate Rejection & `mount-zip` Delegation Policy**:
   - Never attempt complex stream decompression on deflated outer ZIP archives in C++. If an entry is compressed (`compression_method != 0`), exit immediately with code 1 and instruct the user to use `mount-zip`.
5. **Preserve PolicyKit (`pkexec`) Integration**:
   - All privileged operations on `/dev/nbd*` must remain compatible with `pkexec` execution and adhere to `data/polkit/org.blissos.craunbd.policy`.
6. **Maintain Multi-Platform CI/CD Compatibility**:
   - Ensure CMake configuration remains compatible with both `ubuntu-latest` (`glibc`) and `alpine:3.18` (`musl-static` with `-static`).
7. **Keep Documentation & Downstream Tooling Synchronized**:
   - When modifying CLI arguments, update `doc/crau-nbd.1`, `README.md`, `walkthrough.md`, `scripts/crau-nbd-mount.sh`.
