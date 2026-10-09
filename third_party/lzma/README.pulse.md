# LZMA SDK (xz decoder subset)

- Source: https://github.com/ip7z/7zip tag `24.09`, directory `C/` (Igor Pavlov, public domain; see `lzma.txt`).
- Only the files required by the `.xz` decoder (LZMA2 + BCJ filters + CRC32/CRC64/SHA-256) are vendored.
- Used by the Pulse setup bootstrapper (`src/setup/`) to unpack the solid installer payload. Compiled with `Z7_ST` (single-threaded).
