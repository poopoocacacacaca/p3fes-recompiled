// readback only when a capture is requested, independent of the metadata log.
#include "runtime/gs/gs_host_acquired_capture.h"
#include "runtime/gs/gs_host_present_diag.h"
#include "raylib.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
    GSHostAcquiredCapture::Options environmentOptions()
    {
        GSHostAcquiredCapture::Options options;
#ifdef _WIN32
        // narrow getenv uses the ACP; convert the UTF-16 path to UTF-8 without losing Unicode.
        const wchar_t *directory = _wgetenv(L"PS2X_GS_ACQUIRED_CAPTURE_DIR");
        if (!directory || !*directory) return options;
        const auto utf8 = std::filesystem::path(directory).u8string();
        options.directory.assign(reinterpret_cast<const char *>(utf8.data()), utf8.size());
#else
        const char *directory = std::getenv("PS2X_GS_ACQUIRED_CAPTURE_DIR");
        if (!directory || !*directory) return options;
        options.directory = directory;
#endif
        options.fromTick = GSHostPresentDiag::parseFromTick(
            std::getenv("PS2X_GS_ACQUIRED_CAPTURE_FROM_TICK"));
        if (const char *count = std::getenv("PS2X_GS_ACQUIRED_CAPTURE_COUNT"))
        {
            const uint64_t parsed = GSHostPresentDiag::parseFromTick(count);
            // an invalid quota disables the capture instead of raising its cost.
            options.limit = parsed >= 1u && parsed <= 16u ? static_cast<uint32_t>(parsed) : 0u;
        }
        return options;
    }

    struct OwnedImage
    {
        Image image{};
        explicit OwnedImage(Texture2D texture) : image(LoadImageFromTexture(texture)) {}
        ~OwnedImage() { if (image.data) UnloadImage(image); }
        OwnedImage(const OwnedImage &) = delete;
        OwnedImage &operator=(const OwnedImage &) = delete;
    };
}

GSHostAcquiredCapture::GSHostAcquiredCapture() : GSHostAcquiredCapture(environmentOptions()) {}

GSHostAcquiredCapture::GSHostAcquiredCapture(Options selected) : options(std::move(selected))
{
    if (options.limit == 0u || options.limit > 16u) options.directory.clear();
}

bool GSHostAcquiredCapture::capture(const GSSharedPresent::Frame &frame, uint64_t hostTick) noexcept
{
    // with no option, outside the window or with a held sequence there are no GL calls and no I/O.
    if (options.directory.empty() || attempts >= options.limit || hostTick < options.fromTick ||
        frame.sequence == 0u || frame.sequence <= lastSequence || frame.blank ||
        frame.texture == 0u || frame.width == 0u || frame.height == 0u)
        return false;
    lastSequence = frame.sequence;
    const uint32_t index = ++attempts; // also bound failed attempts.
    try
    {
        const auto directory = std::filesystem::u8path(options.directory);
        if (!std::filesystem::is_directory(directory))
            throw std::runtime_error("The private capture folder must exist");
        const std::string name = "acquired_" + std::to_string(index) + "_seq_" +
                                 std::to_string(frame.sequence) + "_tick_" + std::to_string(frame.vsyncTick);
        const auto pngPath = directory / (name + ".png");
        const auto metadataPath = directory / (name + ".json");
        if (std::filesystem::exists(pngPath) || std::filesystem::exists(metadataPath))
            throw std::runtime_error("La captura ya existe; no se sobrescribe");
        const Texture2D borrowed{frame.texture, static_cast<int>(frame.width), static_cast<int>(frame.height),
                                 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        OwnedImage acquired(borrowed); // current reservation, with no other Acquire or VRAM readback.
        if (!acquired.image.data || acquired.image.width != static_cast<int>(frame.width) ||
            acquired.image.height != static_cast<int>(frame.height) ||
            acquired.image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
            throw std::runtime_error("Could not read the whole acquired texture");
        int bytes = 0;
        const std::unique_ptr<unsigned char, decltype(&MemFree)> png(
            ExportImageToMemory(acquired.image, ".png", &bytes), &MemFree);
        if (!png || bytes <= 0) throw std::runtime_error("No se pudo codificar PNG");
        // filesystem/ofstream keep Unicode Windows paths without relying on ANSI fopen.
        std::ofstream imageOutput(pngPath, std::ios::binary);
        imageOutput.write(reinterpret_cast<const char *>(png.get()), bytes);
        imageOutput.close();
        if (!imageOutput) throw std::runtime_error("No se pudo escribir PNG completo");
        std::ofstream metadata(metadataPath, std::ios::binary);
        metadata << "{\n  \"source\": \"acquired_texture\",\n  \"seq\": " << frame.sequence
                 << ",\n  \"render\": " << frame.renderSequence << ",\n  \"hostTick\": " << hostTick
                 << ",\n  \"frameTick\": " << frame.vsyncTick << ",\n  \"displayFbp\": " << frame.displayFbp
                 << ",\n  \"sourceFbp\": " << frame.sourceFbp << ",\n  \"preferred\": " << unsigned(frame.usedPreferred)
                 << ",\n  \"smode2\": " << frame.smode2 << ",\n  \"width\": " << frame.width
                 << ",\n  \"height\": " << frame.height << ",\n  \"hashValid\": " << unsigned(frame.hashValid)
                 << ",\n  \"hash\": \"" << std::hex << std::setw(16) << std::setfill('0') << frame.contentHash
                 << "\",\n  \"blank\": " << unsigned(frame.blank) << "\n}\n";
        metadata.close();
        if (!metadata) throw std::runtime_error("Could not write the complete metadata");
        std::fprintf(stderr, "[gs-present:capture] status=ok seq=%llu frameTick=%llu hostTick=%llu file=%s.png\n",
                     (unsigned long long)frame.sequence, (unsigned long long)frame.vsyncTick,
                     (unsigned long long)hostTick, name.c_str());
        return true;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[gs-present:capture] status=failed seq=%llu reason=%s\n",
                     (unsigned long long)frame.sequence, error.what());
        return false;
    }
}
