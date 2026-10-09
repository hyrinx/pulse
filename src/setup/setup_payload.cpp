#include "setup_payload.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <memory>

extern "C" {
#include "7zCrc.h"
#include "Alloc.h"
#include "Sha256.h"
#include "Xz.h"
#include "XzCrc64.h"
}

namespace pulse::setup {
namespace {

constexpr char kMagic[8] = {'P', 'U', 'L', 'S', 'E', 'P', 'K', '1'};
constexpr size_t kTrailerSize = 8 + 8 + 8 + 32;
constexpr size_t kInputChunk = 1u << 20;

struct HandleCloser {
    void operator()(HANDLE h) const { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

void InitCodecTables() {
    static bool done = false;
    if (done) return;
    CrcGenerateTable();
    Crc64GenerateTable();
    Sha256Prepare();
    done = true;
}

std::wstring Win32Message(const wchar_t* what, DWORD code = GetLastError()) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring message = std::wstring(what) + L" (" + std::to_wstring(code) + L")";
    if (text) { message += L": "; message += text; LocalFree(text); }
    while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r')) message.pop_back();
    return message;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string HexDigest(const Byte* digest) {
    static const char* hex = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) { out[i * 2] = hex[digest[i] >> 4]; out[i * 2 + 1] = hex[digest[i] & 15]; }
    return out;
}

bool ReadExact(HANDLE file, void* buffer, DWORD size) {
    DWORD got = 0;
    auto* p = static_cast<unsigned char*>(buffer);
    while (size) {
        if (!ReadFile(file, p, size, &got, nullptr) || got == 0) return false;
        p += got;
        size -= got;
    }
    return true;
}

bool SeekTo(HANDLE file, uint64_t offset) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(file, li, nullptr, FILE_BEGIN) != FALSE;
}

uint64_t ReadLe64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

// Pull-style reader over the decompressed xz stream.
class XzReader {
public:
    XzReader(HANDLE file, uint64_t offset, uint64_t size) : file_(file), remaining_(size), offset_(offset) {
        XzUnpacker_Construct(&unpacker_, &g_Alloc);
        XzUnpacker_Init(&unpacker_);
        input_.reset(new Byte[kInputChunk]);
    }
    ~XzReader() { XzUnpacker_Free(&unpacker_); }
    XzReader(const XzReader&) = delete;
    XzReader& operator=(const XzReader&) = delete;

    bool Start() { return SeekTo(file_, offset_); }

    // Fills exactly `size` bytes; false on corrupt data, I/O error or early end.
    bool Read(void* dest, size_t size, std::wstring& error) {
        auto* out = static_cast<Byte*>(dest);
        while (size) {
            if (in_pos_ == in_size_ && remaining_) {
                const DWORD want = static_cast<DWORD>(remaining_ < kInputChunk ? remaining_ : kInputChunk);
                if (!ReadExact(file_, input_.get(), want)) { error = Win32Message(L"Reading setup payload failed"); return false; }
                in_pos_ = 0;
                in_size_ = want;
                remaining_ -= want;
            }
            SizeT dest_len = size;
            SizeT src_len = in_size_ - in_pos_;
            ECoderStatus status = CODER_STATUS_NOT_SPECIFIED;
            const SRes res = XzUnpacker_Code(&unpacker_, out, &dest_len, input_.get() + in_pos_, &src_len,
                                             remaining_ == 0 ? 1 : 0, CODER_FINISH_ANY, &status);
            if (res != SZ_OK) { error = L"Setup payload is corrupt (xz error " + std::to_wstring(res) + L")."; return false; }
            in_pos_ += src_len;
            out += dest_len;
            size -= dest_len;
            if (dest_len == 0 && src_len == 0 && in_pos_ == in_size_ && remaining_ == 0) {
                error = L"Setup payload ended unexpectedly.";
                return false;
            }
        }
        return true;
    }

    // Drains the remaining input and confirms the stream checksum.
    bool Finish(std::wstring& error) {
        Byte sink[64];
        for (;;) {
            if (in_pos_ == in_size_ && remaining_) {
                const DWORD want = static_cast<DWORD>(remaining_ < kInputChunk ? remaining_ : kInputChunk);
                if (!ReadExact(file_, input_.get(), want)) { error = Win32Message(L"Reading setup payload failed"); return false; }
                in_pos_ = 0; in_size_ = want; remaining_ -= want;
            }
            SizeT dest_len = sizeof(sink);
            SizeT src_len = in_size_ - in_pos_;
            ECoderStatus status = CODER_STATUS_NOT_SPECIFIED;
            const SRes res = XzUnpacker_Code(&unpacker_, sink, &dest_len, input_.get() + in_pos_, &src_len,
                                             remaining_ == 0 ? 1 : 0, CODER_FINISH_END, &status);
            if (res != SZ_OK) { error = L"Setup payload is corrupt at the end."; return false; }
            in_pos_ += src_len;
            if (dest_len) { error = L"Setup payload has unexpected trailing data."; return false; }
            if (in_pos_ == in_size_ && remaining_ == 0) break;
            if (src_len == 0) break;
        }
        if (!XzUnpacker_IsStreamWasFinished(&unpacker_)) { error = L"Setup payload checksum is incomplete."; return false; }
        return true;
    }

private:
    HANDLE file_;
    uint64_t remaining_;
    uint64_t offset_;
    CXzUnpacker unpacker_{};
    std::unique_ptr<Byte[]> input_;
    size_t in_pos_ = 0;
    size_t in_size_ = 0;
};

// Minimal parser for the manifest written by pack_installer.py.
class Json {
public:
    explicit Json(const std::string& s) : s_(s) {}
    void Ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_; }
    bool Eat(char c) { Ws(); if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; } return false; }
    bool Peek(char c) { Ws(); return i_ < s_.size() && s_[i_] == c; }
    bool String(std::string& out) {
        out.clear();
        if (!Eat('"')) return false;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) return false;
            c = s_[i_++];
            switch (c) {
            case '"': case '\\': case '/': out += c; break;
            case 'n': out += '\n'; break; case 't': out += '\t'; break;
            case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'u': {
                if (i_ + 4 > s_.size()) return false;
                unsigned cp = std::stoul(s_.substr(i_, 4), nullptr, 16);
                i_ += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                    const unsigned lo = std::stoul(s_.substr(i_ + 2, 4), nullptr, 16);
                    i_ += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63)); }
                else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
                else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 63)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Number(uint64_t& v) {
        Ws(); v = 0; size_t start = i_;
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') v = v * 10 + static_cast<uint64_t>(s_[i_++] - '0');
        return i_ > start;
    }
    bool Skip() {  // skips any scalar / nested value
        Ws();
        if (Peek('"')) { std::string t; return String(t); }
        if (Peek('{') || Peek('[')) {
            int depth = 0;
            do {
                Ws();
                if (Peek('"')) { std::string t; if (!String(t)) return false; continue; }
                const char c = s_[i_++];
                if (c == '{' || c == '[') ++depth; else if (c == '}' || c == ']') --depth;
            } while (depth > 0 && i_ < s_.size());
            return depth == 0;
        }
        while (i_ < s_.size() && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != ']') ++i_;
        return true;
    }
private:
    const std::string& s_;
    size_t i_ = 0;
};

bool ParseManifest(const std::string& text, PayloadInfo& info) {
    Json j(text);
    if (!j.Eat('{')) return false;
    if (j.Eat('}')) return false;
    do {
        std::string key;
        if (!j.String(key) || !j.Eat(':')) return false;
        if (key == "version") { if (!j.String(info.version)) return false; }
        else if (key == "channel") { if (!j.String(info.channel)) return false; }
        else if (key == "min_windows_build") { if (!j.Number(info.min_windows_build)) return false; }
        else if (key == "files") {
            if (!j.Eat('[')) return false;
            if (!j.Eat(']')) {
                do {
                    if (!j.Eat('{')) return false;
                    PayloadFile f;
                    std::string path;
                    do {
                        std::string k;
                        if (!j.String(k) || !j.Eat(':')) return false;
                        if (k == "path") { if (!j.String(path)) return false; }
                        else if (k == "size") { if (!j.Number(f.size)) return false; }
                        else if (k == "sha256") { if (!j.String(f.sha256)) return false; }
                        else if (!j.Skip()) return false;
                    } while (j.Eat(','));
                    if (!j.Eat('}')) return false;
                    f.path = Utf8ToWide(path);
                    for (auto& ch : f.path) if (ch == L'/') ch = L'\\';
                    // Reject absolute paths and traversal before anything touches disk.
                    if (f.path.empty() || f.path[0] == L'\\' || f.path.find(L':') != std::wstring::npos ||
                        f.path.find(L"..") != std::wstring::npos || f.sha256.size() != 64)
                        return false;
                    info.total_bytes += f.size;
                    info.files.push_back(std::move(f));
                } while (j.Eat(','));
                if (!j.Eat(']')) return false;
            }
        } else if (!j.Skip()) return false;
    } while (j.Eat(','));
    return j.Eat('}') && !info.files.empty();
}


// End of the packed image. Authenticode signing appends a certificate table
// (PE security directory; its address is a file offset) after the trailer,
// so a signed setup ends where that table starts.
uint64_t SignedImageEnd(HANDLE file, uint64_t file_size) {
    unsigned char dos[64];
    if (!SeekTo(file, 0) || !ReadExact(file, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z') return file_size;
    const uint32_t pe = static_cast<uint32_t>(dos[60]) | (static_cast<uint32_t>(dos[61]) << 8) |
                        (static_cast<uint32_t>(dos[62]) << 16) | (static_cast<uint32_t>(dos[63]) << 24);
    unsigned char head[4 + 20 + 2];
    if (pe == 0 || pe > file_size || !SeekTo(file, pe) || !ReadExact(file, head, sizeof(head)) ||
        std::memcmp(head, "PE\0\0", 4) != 0) return file_size;
    const unsigned magic = static_cast<unsigned>(head[24]) | (static_cast<unsigned>(head[25]) << 8);
    const uint64_t directories = pe + 24ull + (magic == 0x20b ? 112u : magic == 0x10b ? 96u : 0u);
    if (directories == pe + 24ull) return file_size;
    unsigned char security[8];
    if (!SeekTo(file, directories + 4 * 8) || !ReadExact(file, security, sizeof(security))) return file_size;
    const uint64_t offset = ReadLe64(security) & 0xffffffffull;
    const uint64_t size = ReadLe64(security) >> 32;
    if (offset == 0 || size == 0 || offset + size != file_size) return file_size;
    return offset;
}

}  // namespace

bool Payload::Open(const std::filesystem::path& image, std::wstring& error) {
    InitCodecTables();
    image_ = image;
    info_ = {};
    UniqueHandle file(CreateFileW(image.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) { file.release(); error = Win32Message(L"Cannot open setup image"); return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || static_cast<uint64_t>(size.QuadPart) < kTrailerSize) {
        error = L"Setup image has no payload.";
        return false;
    }
    const uint64_t file_size = static_cast<uint64_t>(size.QuadPart);
    const uint64_t image_end = SignedImageEnd(file.get(), file_size);
    // The packer aligns the image to 8 bytes; tolerate alignment zeros that a
    // signing tool may still insert before its certificate table.
    unsigned char trailer[kTrailerSize];
    uint64_t trailer_at = 0;
    bool found = false;
    for (uint64_t pad = 0; pad < 8 && !found && image_end >= kTrailerSize + pad; ++pad) {
        trailer_at = image_end - kTrailerSize - pad;
        if (!SeekTo(file.get(), trailer_at) || !ReadExact(file.get(), trailer, kTrailerSize)) {
            error = Win32Message(L"Cannot read setup trailer");
            return false;
        }
        found = std::memcmp(trailer, kMagic, sizeof(kMagic)) == 0;
    }
    if (!found) { error = L"Setup image has no payload."; return false; }
    info_.payload_offset = ReadLe64(trailer + 8);
    info_.payload_size = ReadLe64(trailer + 16);
    const uint64_t payload_end = info_.payload_offset + info_.payload_size;
    if (info_.payload_size == 0 || payload_end < info_.payload_offset || payload_end > trailer_at ||
        trailer_at - payload_end >= 8) {
        error = L"Setup payload bounds are invalid.";
        return false;
    }

    // Verify the compressed stream before trusting any manifest field.
    CSha256 sha;
    Sha256_Init(&sha);
    std::unique_ptr<Byte[]> buffer(new Byte[kInputChunk]);
    if (!SeekTo(file.get(), info_.payload_offset)) { error = Win32Message(L"Cannot seek setup payload"); return false; }
    for (uint64_t left = info_.payload_size; left;) {
        const DWORD want = static_cast<DWORD>(left < kInputChunk ? left : kInputChunk);
        if (!ReadExact(file.get(), buffer.get(), want)) { error = Win32Message(L"Cannot read setup payload"); return false; }
        Sha256_Update(&sha, buffer.get(), want);
        left -= want;
    }
    Byte digest[SHA256_DIGEST_SIZE];
    Sha256_Final(&sha, digest);
    if (std::memcmp(digest, trailer + 24, 32) != 0) {
        error = L"Setup file is damaged (payload checksum mismatch). Please download it again.";
        return false;
    }
    return ReadManifest(error);
}

bool Payload::ReadManifest(std::wstring& error) {
    UniqueHandle file(CreateFileW(image_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) { file.release(); error = Win32Message(L"Cannot open setup image"); return false; }
    XzReader reader(file.get(), info_.payload_offset, info_.payload_size);
    unsigned char header[8];
    if (!reader.Start() || !reader.Read(header, sizeof(header), error)) return false;
    if (std::memcmp(header, "PPKM", 4) != 0) { error = L"Setup manifest is missing."; return false; }
    const uint32_t len = header[4] | (header[5] << 8) | (header[6] << 16) | (static_cast<uint32_t>(header[7]) << 24);
    if (len == 0 || len > (16u << 20)) { error = L"Setup manifest size is invalid."; return false; }
    std::string text(len, '\0');
    if (!reader.Read(text.data(), len, error)) return false;
    if (!ParseManifest(text, info_)) { error = L"Setup manifest is invalid."; return false; }
    return true;
}

bool Payload::ExtractTo(const std::filesystem::path& target, const ProgressCallback& progress, std::wstring& error) {
    UniqueHandle image(CreateFileW(image_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (image.get() == INVALID_HANDLE_VALUE) { image.release(); error = Win32Message(L"Cannot open setup image"); return false; }
    XzReader reader(image.get(), info_.payload_offset, info_.payload_size);
    unsigned char header[8];
    if (!reader.Start() || !reader.Read(header, sizeof(header), error)) return false;
    const uint32_t len = header[4] | (header[5] << 8) | (header[6] << 16) | (static_cast<uint32_t>(header[7]) << 24);
    std::string skip(len, '\0');
    if (!reader.Read(skip.data(), len, error)) return false;

    std::unique_ptr<Byte[]> buffer(new Byte[kInputChunk]);
    ExtractProgress state;
    state.total_bytes = info_.total_bytes;
    for (const auto& f : info_.files) {
        state.current = &f;
        if (progress && !progress(state)) { error = L"Canceled."; return false; }
        const auto final_path = target / f.path;
        std::error_code ec;
        std::filesystem::create_directories(final_path.parent_path(), ec);
        if (ec) { error = L"Cannot create folder " + final_path.parent_path().wstring(); return false; }
        auto temp_path = final_path;
        temp_path += L".pulse-new";
        UniqueHandle out(CreateFileW(temp_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (out.get() == INVALID_HANDLE_VALUE) { out.release(); error = Win32Message((L"Cannot write " + temp_path.wstring()).c_str()); return false; }
        CSha256 sha;
        Sha256_Init(&sha);
        for (uint64_t left = f.size; left;) {
            const size_t want = static_cast<size_t>(left < kInputChunk ? left : kInputChunk);
            if (!reader.Read(buffer.get(), want, error)) { out.reset(); DeleteFileW(temp_path.c_str()); return false; }
            Sha256_Update(&sha, buffer.get(), want);
            DWORD written = 0;
            if (!WriteFile(out.get(), buffer.get(), static_cast<DWORD>(want), &written, nullptr) || written != want) {
                error = Win32Message((L"Cannot write " + temp_path.wstring()).c_str());
                out.reset(); DeleteFileW(temp_path.c_str());
                return false;
            }
            left -= want;
            state.done_bytes += want;
            if (progress && !progress(state)) { out.reset(); DeleteFileW(temp_path.c_str()); error = L"Canceled."; return false; }
        }
        Byte digest[SHA256_DIGEST_SIZE];
        Sha256_Final(&sha, digest);
        out.reset();
        if (HexDigest(digest) != f.sha256) {
            DeleteFileW(temp_path.c_str());
            error = L"Checksum mismatch for " + f.path;
            return false;
        }
        if (!MoveFileExW(temp_path.c_str(), final_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            error = Win32Message((L"Cannot replace " + final_path.wstring()).c_str());
            DeleteFileW(temp_path.c_str());
            return false;
        }
    }
    return reader.Finish(error);
}

}  // namespace pulse::setup
