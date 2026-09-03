# crau-nbd

**crau-nbd** is a high-performance Linux Network Block Device (NBD) daemon designed for direct, read-only block mounting of partitions inside Android and ChromeOS Over-The-Air (`payload.bin`) images with **zero intermediate disk extraction**.

It is developed in strict accordance with the **[GNU Coding Standards](https://www.gnu.org/prep/standards/standards.html)** and **IEEE Std 1003.1 (POSIX.1)** Base Specifications.

---

## Key Features

* **Zero Disk Extraction**: Mount 5+ GB Android `system` or `vendor` partitions directly from an OTA image using **0 bytes** of physical scratch disk space.
* **Direct Uncompressed ZIP Seeking**: Directly maps `payload.bin` from uncompressed (`Stored`) OTA ZIP archives (standard in LineageOS, Bliss OS, and Pixel updates) using zero-copy file-descriptor offsets.
* **Deflate Rejection & `mount-zip` Integration**: Complies with GNU diagnostics by cleanly identifying deflated outer archives and instructing users to mount them via [google/mount-zip](https://github.com/google/mount-zip).
* **High-Speed Decompression**: Multi-threaded extent streaming supporting `REPLACE` (raw), `REPLACE_XZ` (LZMA via `liblzma`), and `REPLACE_BZ` (bzip2 via `libbz2`).
* **LRU Block Cache**: In-memory cache for 4KB filesystem reads, drastically reducing read amplification during kernel filesystem metadata traversal (`ext4` and `erofs`).
* **PolicyKit (`sudo`) Integration**: Full support for supervised privilege elevation.

---

## Command Line Interface

```text
Usage: crau-nbd [OPTION]... COMMAND [FILE]
Map and mount partitions inside Android/ChromeOS OTA payload.bin images.

Commands:
  list FILE                    List partitions and compression statistics
  attach -d DEV -p PART FILE   Attach partition to NBD device
  detach -d DEV                Detach and disconnect NBD device

Options:
  -d, --device=DEV             target NBD block device node (e.g., /dev/nbd0)
  -p, --partition=NAME         partition name to expose (e.g., system)
  -c, --cache-size=MB          size of in-memory LRU block cache in MB (default: 128)
  -v, --verbose                explain what is being done
  -h, --help                   display this help and exit
  -V, --version                output version information and exit
```

---

## Quick Start Example

```bash
# 1. Ensure NBD kernel driver is loaded
sudo modprobe nbd

# 2. List partitions inside the OTA package (unprivileged)
crau-nbd list lineage-21.0-x86_64-signed.zip

# 3. Attach system partition to /dev/nbd0 using sudo (supervised elevation)
sudo crau-nbd attach -d /dev/nbd0 -p system lineage-21.0-x86_64-signed.zip &

# 4. Mount partition read-only
sudo mkdir -p /mnt/system
sudo mount -o ro /dev/nbd0 /mnt/system

# 5. Inspect contents
cat /mnt/system/system/build.prop

# 6. Unmount and detach cleanly
sudo umount /mnt/system
sudo crau-nbd detach -d /dev/nbd0
```

---

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
