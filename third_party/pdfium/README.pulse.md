# Pinned PDFium runtime

- Binary distribution: https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium%2F8044
- Asset: https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8044/pdfium-win-x64.tgz
- Release published: 2026-09-07.
- Asset SHA-256: `78a17d9a5f14467631c26a3ac8741b27a0471ecc05bd6a119b523598160a0537` (verified against GitHub release asset digest).
- Package VERSION: 155.0.8044.0. Windows x64, `pdf_enable_v8=false`, `pdf_enable_xfa=false`; see original `args.gn`.
- API headers and DLL originate from the same archive. Only the isolated `Pulse.Document.exe` loads `pdfium.dll`, by absolute application-directory path. The UI and filename service do not load it.
- Keep root `LICENSE` (binary distribution) and **all** `licenses/` notices. The underlying PDFium license is `licenses/pdfium.txt`; the distribution's root MIT license alone is insufficient.
- CMake copies the DLL and complete license directory to the build output; the setup packer (`tools/pack_installer`) packages both. Update the pinned binary, headers, version, build configuration, checksum, and notices together.

The downloaded archive is ignored, while `bin/pdfium.dll` is explicitly included in source control. Upstream public interfaces: https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdfview.h and https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_text.h . No V8, XFA, OCR, rendering UI, or persistent body index is added by this integration.
