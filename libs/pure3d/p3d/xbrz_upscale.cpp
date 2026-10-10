// Android-only runtime xBRZ 2x texture upscaling.
// Original textures are left untouched if a format, allocation, or texture-size
// check fails. The returned texture is owned by the caller and replaces source.
#include <p3d/texture.hpp>
#include <p3d/context.hpp>
#ifdef RAD_ANDROID
#include <android/log.h>
#endif // RAD_ANDROID
#include <pddi/pddi.hpp>
#include <pddi/pddienum.hpp>
#include <pddi/gles/decompress.hpp>
#include <p3d/xbrz/xbrz.h>
#include <vector>
#include <cstring>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>

#ifdef RAD_ANDROID
namespace
{
    const int MAX_XBRZ_TEXTURE_DIMENSION = 4096;
    const size_t MAX_XBRZ_TEXTURE_PIXELS = (size_t)8 * 1024 * 1024;

    void LogReadMipFailure(const char* diagnosticPath, const char* diagnosticName,
                        int mip, const char* reason, const pddiLockInfo* lock,
                        int expectedWidth, int expectedHeight)
    {
        __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
            "[XBRZ-DIAG] READ_FAIL path=%s name=%s mip=%d reason=%s expected=%dx%d lockFormat=%d lockSize=%dx%d pitch=%d bitsPresent=%d native=%d alphaMask=%08x shiftsR=%d/%d shiftsG=%d/%d shiftsB=%d/%d shiftsA=%d/%d",
            diagnosticPath ? diagnosticPath : "unspecified",
            diagnosticName ? diagnosticName : "(unnamed)", mip, reason,
            expectedWidth, expectedHeight,
            lock ? (int)lock->format : -1,
            lock ? lock->width : -1, lock ? lock->height : -1,
            lock ? lock->pitch : 0, lock && lock->bits ? 1 : 0,
            lock ? (lock->native ? 1 : 0) : 0,
            lock ? lock->rgbaMask[3] : 0,
            lock ? lock->rgbaLShift[0] : 0, lock ? lock->rgbaRShift[0] : 0,
            lock ? lock->rgbaLShift[1] : 0, lock ? lock->rgbaRShift[1] : 0,
            lock ? lock->rgbaLShift[2] : 0, lock ? lock->rgbaRShift[2] : 0,
            lock ? lock->rgbaLShift[3] : 0, lock ? lock->rgbaRShift[3] : 0);
    }

    bool ReadArgbMip(tTexture* texture, int mip, int expectedWidth,
                     int expectedHeight, std::vector<uint32_t>& pixels,
                     const char* diagnosticPath, const char* diagnosticName)
    {
        pixels.resize((size_t)expectedWidth * (size_t)expectedHeight);
        pddiLockInfo* lock = texture->Lock(mip);
        if (lock == NULL)
        {
            LogReadMipFailure(diagnosticPath, diagnosticName, mip, "LOCK_NULL",
                              lock, expectedWidth, expectedHeight);
            return false;
        }
        if (lock->bits == NULL)
        {
            LogReadMipFailure(diagnosticPath, diagnosticName, mip, "BITS_NULL",
                              lock, expectedWidth, expectedHeight);
            texture->Unlock(mip);
            return false;
        }
        if (lock->width != expectedWidth || lock->height != expectedHeight)
        {
            LogReadMipFailure(diagnosticPath, diagnosticName, mip, "DIMENSIONS",
                              lock, expectedWidth, expectedHeight);
            texture->Unlock(mip);
            return false;
        }

        // Support the compressed formats used by the existing GLES backend.
        // Lock() exposes DXT data as block-compressed bytes, not 4 bytes/pixel.
        // Decode only the formats the renderer itself knows how to decode;
        // unsupported DXT2/DXT4 and other formats still fall back unchanged.
        const bool dxt1 = lock->format == PDDI_PIXEL_DXT1;
        const bool dxt3 = lock->format == PDDI_PIXEL_DXT3;
        const bool dxt5 = lock->format == PDDI_PIXEL_DXT5;
        if (dxt1 || dxt3 || dxt5)
        {
            const size_t blocksWide = ((size_t)expectedWidth + 3U) / 4U;
            const size_t blocksHigh = ((size_t)expectedHeight + 3U) / 4U;
            const size_t blockBytes = dxt1 ? 8U : 16U;
            const size_t compressedRowBytes = blocksWide * blockBytes;
            if (lock->pitch <= 0 ||
                (size_t)lock->pitch < compressedRowBytes)
            {
                LogReadMipFailure(diagnosticPath, diagnosticName, mip,
                                  "COMPRESSED_PITCH_TOO_SMALL",
                                  lock, expectedWidth, expectedHeight);
                texture->Unlock(mip);
                return false;
            }

            // The decoder expects tightly packed block rows. Copy using the
            // pitch reported by the backend so padding cannot corrupt a row.
            std::vector<uint8_t> compressed(compressedRowBytes * blocksHigh);
            const uint8_t* compressedRow = (const uint8_t*)lock->bits;
            for (size_t y = 0; y < blocksHigh; ++y)
            {
                memcpy(compressed.data() + y * compressedRowBytes,
                       compressedRow + y * (size_t)lock->pitch,
                       compressedRowBytes);
            }

            std::vector<unsigned char> rgba(
                (size_t)expectedWidth * (size_t)expectedHeight * 4U);
            if (dxt1)
            {
                BlockDecompressImageBC1((uint32_t)expectedWidth,
                                        (uint32_t)expectedHeight,
                                        compressed.data(), rgba.data());
            }
            else if (dxt3)
            {
                BlockDecompressImageBC2((uint32_t)expectedWidth,
                                        (uint32_t)expectedHeight,
                                        compressed.data(), rgba.data());
            }
            else
            {
                BlockDecompressImageBC3((uint32_t)expectedWidth,
                                        (uint32_t)expectedHeight,
                                        compressed.data(), rgba.data());
            }

            // decompress.hpp returns bytes in RGBA order. Convert to the
            // canonical numeric 0xAARRGGBB format used by xBRZ and retain the
            // original alpha channel (including DXT1 transparent pixels).
            pixels.resize((size_t)expectedWidth * (size_t)expectedHeight);
            for (size_t i = 0; i < pixels.size(); ++i)
            {
                const uint32_t r = rgba[i * 4U + 0U];
                const uint32_t g = rgba[i * 4U + 1U];
                const uint32_t b = rgba[i * 4U + 2U];
                const uint32_t a = rgba[i * 4U + 3U];
                pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
            }
            texture->Unlock(mip);
            __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
                "[XBRZ-DIAG] FORMAT_DECODED path=%s name=%s mip=%d codec=%s size=%dx%d",
                diagnosticPath ? diagnosticPath : "unspecified",
                diagnosticName ? diagnosticName : "(unnamed)", mip,
                dxt1 ? "DXT1" : (dxt3 ? "DXT3" : "DXT5"),
                expectedWidth, expectedHeight);
            return true;
        }

        // GLES stores RGB888 textures in a 32-bit backing buffer, but the
        // upper byte is not an alpha channel. Accept it only as 4 bytes/pixel,
        // then explicitly force alpha opaque before passing pixels to xBRZ.
        const bool rgb888 = lock->format == PDDI_PIXEL_RGB888;
        if (lock->format != PDDI_PIXEL_ARGB8888 && !rgb888)
        {
            LogReadMipFailure(diagnosticPath, diagnosticName, mip, "FORMAT",
                              lock, expectedWidth, expectedHeight);
            texture->Unlock(mip);
            return false;
        }
        if (lock->pitch == 0)
        {
            LogReadMipFailure(diagnosticPath, diagnosticName, mip, "ZERO_PITCH",
                              lock, expectedWidth, expectedHeight);
            texture->Unlock(mip);
            return false;
        }
        if ((lock->pitch < 0 ? -lock->pitch : lock->pitch) < expectedWidth * 4)
        {
            LogReadMipFailure(diagnosticPath, diagnosticName, mip, "PITCH_TOO_SMALL",
                              lock, expectedWidth, expectedHeight);
            texture->Unlock(mip);
            return false;
        }

        const unsigned char* row = (const unsigned char*)lock->bits;
        for (int y = 0; y < expectedHeight; ++y)
        {
            for (int x = 0; x < expectedWidth; ++x)
            {
                uint32_t packed = 0;
                memcpy(&packed, row + (size_t)x * 4, sizeof(packed));
                // Convert backend-packed pixels to canonical 0xAARRGGBB
                // ordering before passing them to xBRZ.
                uint32_t canonical =
                    (((packed & lock->rgbaMask[0]) >> lock->rgbaLShift[0]) << lock->rgbaRShift[0]) |
                    (((packed & lock->rgbaMask[1]) >> lock->rgbaLShift[1]) << lock->rgbaRShift[1]) |
                    (((packed & lock->rgbaMask[2]) >> lock->rgbaLShift[2]) << lock->rgbaRShift[2]) |
                    (((packed & lock->rgbaMask[3]) >> lock->rgbaLShift[3]) << lock->rgbaRShift[3]);
                if (rgb888)
                {
                    canonical |= 0xff000000U;
                }
                pixels[(size_t)y * (size_t)expectedWidth + (size_t)x] = canonical;
            }
            row += lock->pitch;
        }
        texture->Unlock(mip);
        return true;
    }

    bool IsFullyOpaque(const std::vector<uint32_t>& pixels)
    {
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            if ((pixels[i] & 0xff000000U) != 0xff000000U)
            {
                return false;
            }
        }
        return true;
    }

    // Global experiment policy: any texture-loading path may attempt xBRZ.
    // The scaler's dimension, memory, format, and allocation guards still apply;
    // anything unsupported is left as the original texture.
    bool AllowsXbrzUpscaling(const char* diagnosticPath)
    {
        return diagnosticPath != NULL && diagnosticPath[0] != '\0';
    }

    // Alpha-bearing textures are included in this experiment. The ARGB-aware
    // scaler handles alpha, while DXT1/3/5 inputs are decoded before scaling.
    bool AllowsAlphaUpscaling(const char* diagnosticPath)
    {
        return diagnosticPath != NULL && diagnosticPath[0] != '\0';
    }

    bool WriteArgbMip(tTexture* texture, int mip, int expectedWidth,
                      int expectedHeight, const std::vector<uint32_t>& pixels)
    {
        const size_t count = (size_t)expectedWidth * (size_t)expectedHeight;
        if (pixels.size() != count) return false;
        pddiLockInfo* lock = texture->Lock(mip);
        if (lock == NULL || lock->bits == NULL)
        {
            if (lock != NULL) texture->Unlock(mip);
            return false;
        }
        if (lock->format != PDDI_PIXEL_ARGB8888 ||
            lock->width != expectedWidth || lock->height != expectedHeight ||
            lock->pitch == 0 ||
            (lock->pitch < 0 ? -lock->pitch : lock->pitch) < expectedWidth * 4)
        {
            texture->Unlock(mip);
            return false;
        }

        unsigned char* row = (unsigned char*)lock->bits;
        for (int y = 0; y < expectedHeight; ++y)
        {
            for (int x = 0; x < expectedWidth; ++x)
            {
                const uint32_t canonical =
                    pixels[(size_t)y * (size_t)expectedWidth + (size_t)x];
                const uint32_t packed = (uint32_t)lock->MakeColour(canonical);
                memcpy(row + (size_t)x * 4, &packed, sizeof(packed));
            }
            row += lock->pitch;
        }
        texture->Unlock(mip);
        return true;
    }
}

tTexture* CreateXbrz2xTexture(tTexture* source, const char* loadPath)
{
    const char* diagnosticPath = loadPath ? loadPath : "unspecified";
    if (source == NULL)
    {
        __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
            "[XBRZ-DIAG] SKIP_EARLY path=%s reason=SOURCE_NULL", diagnosticPath);
        return NULL;
    }
    if (source->GetTexture() == NULL)
    {
        __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
            "[XBRZ-DIAG] SKIP_EARLY path=%s reason=TEXTURE_NULL name=%s",
            diagnosticPath, source->GetNameDangerous() ? source->GetNameDangerous() : "(unnamed)");
        return NULL;
    }
    if (source->HasOriginalSize())
    {
        __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
            "[XBRZ-DIAG] SKIP_EARLY path=%s reason=ALREADY_UPSCALED name=%s original=%dx%d",
            diagnosticPath, source->GetNameDangerous() ? source->GetNameDangerous() : "(unnamed)",
            source->GetOriginalWidth(), source->GetOriginalHeight());
        return NULL;
    }
    const int width = source->GetWidth();
    const int height = source->GetHeight();
    const int lastSourceMip = source->GetNumMipMaps();
    if (width <= 0 || height <= 0 ||
        width > MAX_XBRZ_TEXTURE_DIMENSION / 2 ||
        height > MAX_XBRZ_TEXTURE_DIMENSION / 2 ||
        lastSourceMip < 0 || lastSourceMip > 12)
    {
        __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
            "[XBRZ-DIAG] SKIP_EARLY path=%s reason=INVALID_DIM_OR_MIP size=%dx%d sourceLastMip=%d",
            diagnosticPath, width, height, lastSourceMip);
        return NULL;
    }

    const int targetWidth = width * 2;
    const int targetHeight = height * 2;
    const size_t targetPixels = (size_t)targetWidth * (size_t)targetHeight;
    if (targetPixels > MAX_XBRZ_TEXTURE_PIXELS) return NULL;

    const char* diagnosticName = source->GetNameDangerous();
    if (diagnosticName == NULL || diagnosticName[0] == '\0')
        diagnosticName = "(unnamed)";
    // Global xBRZ 2x experiment: include GUI, world, character, vehicle,
    // and effects textures on all supported loading paths. Unsupported formats
    // and textures over the existing safety limits retain their original data.
    if (!AllowsXbrzUpscaling(diagnosticPath))
    {
        __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
            "[XBRZ-DIAG] SKIP_EARLY path=%s reason=PATH_NOT_ALLOWED name=%s size=%dx%d",
            diagnosticPath, diagnosticName, width, height);
        return NULL;
    }

    const bool allowAlphaUpscaling = AllowsAlphaUpscaling(diagnosticPath);
    __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] CANDIDATE path=%s name=%s size=%dx%d depth=%d pixelFormat=%d alphaDepth=%d sourceLastMip=%d\n",
                diagnosticPath, diagnosticName, width, height, source->GetDepth(),
                (int)source->GetPixelFormat(), source->GetAlphaDepth(), lastSourceMip);

    tTexture* target = new tTexture;
    if (!target->Create(targetWidth, targetHeight, 32, 8,
                       lastSourceMip + 1, PDDI_TEXTYPE_RGB, PDDI_USAGE_STATIC) ||
        target->GetTexture() == NULL ||
        target->GetWidth() != targetWidth || target->GetHeight() != targetHeight)
    {
        __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
            "[XBRZ-DIAG] SKIP_EARLY path=%s reason=TARGET_CREATE_FAILED source=%dx%d target=%dx%d",
            diagnosticPath, width, height, targetWidth, targetHeight);
        target->Release();
        return NULL;
    }

    try
    {
        std::vector<uint32_t> sourcePixels;
        std::vector<uint32_t> scaledPixels;
        sourcePixels.resize((size_t)width * (size_t)height);
        scaledPixels.resize(targetPixels);
        if (!ReadArgbMip(source, 0, width, height, sourcePixels, diagnosticPath, diagnosticName))
        {
            __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] SKIP_BASE_READ path=%s name=%s size=%dx%d\n",
                        diagnosticPath, diagnosticName, width, height);
            target->Release();
            return NULL;
        }
        if (!IsFullyOpaque(sourcePixels))
        {
            if (!allowAlphaUpscaling)
            {
                // Keep alpha-bearing world/effect textures unchanged in this
                // GUI-first experiment.
                __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] SKIP_BASE_ALPHA path=%s name=%s size=%dx%d\n",
                            diagnosticPath, diagnosticName, width, height);
                target->Release();
                return NULL;
            }
            // xBRZ's ARGB mode blends colors with their alpha and preserves the
            // channel in the scaled image. Limit this to frontend.p3d for now.
            __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] ALPHA_ENABLED path=%s name=%s size=%dx%d\n",
                        diagnosticPath, diagnosticName, width, height);
        }

        xbrz::scale(2, sourcePixels.data(), scaledPixels.data(),
                    width, height, xbrz::ColorFormat::ARGB_UNBUFFERED);
        if (!WriteArgbMip(target, 0, targetWidth, targetHeight, scaledPixels))
        {
            __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG",
                "[XBRZ-DIAG] SKIP_BASE_WRITE path=%s name=%s target=%dx%d",
                diagnosticPath, diagnosticName, targetWidth, targetHeight);
            target->Release();
            return NULL;
        }

        // Shift the source mip chain by one level. Output mip 1 retains the
        // original base image; output mip 2 retains source mip 1, and so on.
        for (int mip = 0; mip <= lastSourceMip; ++mip)
        {
            const int mipWidth = width >> mip;
            const int mipHeight = height >> mip;
            if (mipWidth <= 0 || mipHeight <= 0)
            {
                __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] SKIP_MIP_DIM path=%s name=%s mip=%d\n",
                            diagnosticPath, diagnosticName, mip);
                target->Release();
                return NULL;
            }
            if (!ReadArgbMip(source, mip, mipWidth, mipHeight, sourcePixels, diagnosticPath, diagnosticName))
            {
                __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] SKIP_MIP_READ path=%s name=%s mip=%d\n",
                            diagnosticPath, diagnosticName, mip);
                target->Release();
                return NULL;
            }
            if (!allowAlphaUpscaling && !IsFullyOpaque(sourcePixels))
            {
                __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] SKIP_MIP_ALPHA path=%s name=%s mip=%d\n",
                            diagnosticPath, diagnosticName, mip);
                target->Release();
                return NULL;
            }
            if (!WriteArgbMip(target, mip + 1, mipWidth, mipHeight, sourcePixels))
            {
                __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] SKIP_MIP_WRITE path=%s name=%s mip=%d\n",
                            diagnosticPath, diagnosticName, mip);
                target->Release();
                return NULL;
            }
        }
    }
    catch (...)
    {
        // Allocation or scaler failure falls back to the original texture.
        target->Release();
        return NULL;
    }

    target->SetName(source->GetNameDangerous());
    target->SetPriority(source->GetPriority());
    target->SetOriginalSize(width, height);
    __android_log_print(ANDROID_LOG_INFO, "XBRZ-DIAG", "[XBRZ-DIAG] UPSCALED path=%s name=%s from=%dx%d to=%dx%d\n",
                diagnosticPath, diagnosticName, width, height, targetWidth, targetHeight);
    return target;
}
#endif // RAD_ANDROID
