#include "core.hpp"
#include <miniz_tinfl.h>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

namespace launcher {
static uint64_t little64(const char* p) {
    uint64_t n = 0;
    for (unsigned i = 0; i < 8; ++i)
        n |= (uint64_t)(unsigned char)p[i] << (i * 8);
    return n;
}
Paths prepare() {
    const auto self = executablePath();
    std::ifstream in(self, std::ios::binary);
    in.seekg(0, std::ios::end);
    const auto total = (uint64_t)in.tellg();
    char footer[24]{};
    if (total >= 24) {
        in.seekg((std::streamoff)total - 24);
        in.read(footer, 24);
    }
    if (!in || std::memcmp(footer + 16, "DTVRBND1", 8))
        throw std::runtime_error("Embedded VR files missing. Build the bundled launcher first.");
    const auto indexSize = little64(footer), payloadSize = little64(footer + 8);
    if (indexSize > 1024 * 1024 || indexSize < 2 || payloadSize < indexSize ||
        payloadSize > 128 * 1024 * 1024 || payloadSize > total - 24)
        throw std::runtime_error("Invalid embedded VR bundle size");
    const uint64_t payloadStart = total - 24 - payloadSize, indexStart = total - 24 - indexSize;
    std::string index((size_t)indexSize, 0);
    in.seekg((std::streamoff)indexStart);
    in.read(index.data(), (std::streamsize)index.size());
    const auto j = Json::parse(index);
    const auto files = j.at("files");
    if (!files.is_array() || files.size() > 32)
        throw std::runtime_error("Invalid embedded file list");
    Paths dirs;
    dirs.config = configPath();
    dirs.logs = statePath() / "logs";
    dirs.cache = cachePath() / sha256(index).substr(0, 24);
    dirs.binaries = dirs.cache / "bin";
    privateDirectory(dirs.config);
    privateDirectory(dirs.cache);
    privateDirectory(dirs.logs);
    const std::set<std::string> allowed = {"bin/dirttown-vr.dll",
                                           "bin/vr-launch.exe",
                                           "bin/vr-probe.exe",
                                           "bin/temporal-test.exe",
                                           "bin/core-test.exe",
                                           "bin/eye-transfer-test.exe",
                                           "bin/openxr_loader.dll",
                                           "controls.example.ini",
                                           "README.rst",
                                           "LICENSE"};
    std::set<std::string> seen;
    for (const auto& item : files) {
        const auto name = item.at("name").get<std::string>();
        const auto relative = path(name);
        const bool license = relative.parent_path() == "licenses" &&
                             relative.extension() == ".txt" && name.find('\\') == std::string::npos;
        if ((!allowed.count(name) && !license) || !seen.insert(name).second)
            throw std::runtime_error("Invalid embedded file name");
        const uint64_t size = item.at("size"), packed = item.at("packed"),
                       offset = item.at("offset");
        const auto hash = item.at("sha256").get<std::string>();
        if (size > 64 * 1024 * 1024 || packed > 64 * 1024 * 1024 ||
            offset > payloadSize - indexSize || packed > payloadSize - indexSize - offset ||
            hash.size() != 64)
            throw std::runtime_error("Invalid embedded file bounds");
        const auto target = dirs.cache / relative;
        if (!fs::is_symlink(target) && fs::is_regular_file(target) && fileHash(target) == hash)
            continue;
        std::string compressed((size_t)packed, 0), data((size_t)size, 0);
        in.seekg((std::streamoff)(payloadStart + offset));
        in.read(compressed.data(), (std::streamsize)packed);
        const auto count =
            tinfl_decompress_mem_to_mem(data.data(), data.size(), compressed.data(),
                                        compressed.size(), TINFL_FLAG_PARSE_ZLIB_HEADER);
        if (!in || count != size || sha256(data) != hash)
            throw std::runtime_error("Corrupt embedded VR file: " + name);
        writeFile(target, data);
    }
    for (const auto& name : allowed)
        if (!seen.count(name))
            throw std::runtime_error("Incomplete embedded VR bundle");
    auto origin = self.parent_path();
    if (origin.filename() == "bin" && origin.parent_path().filename() == "vr" &&
        origin.parent_path().parent_path().filename() == ".local")
        origin = origin.parent_path().parent_path().parent_path();
    dirs.legacy = origin / ".local";
    if (!fs::exists(dirs.config / "controls.ini")) {
        auto old = dirs.legacy / "controls.ini";
        writeFile(dirs.config / "controls.ini",
                  readFile(fs::is_regular_file(old) ? old : dirs.cache / "controls.example.ini"));
    }
    return dirs;
}
} // namespace launcher
