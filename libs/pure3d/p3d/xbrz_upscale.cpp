// Android-only runtime xBRZ 2x texture upscaling.
// Original textures are left untouched if a format, allocation, or texture-size
// check fails. The returned texture is owned by the caller and replaces source.
#include <p3d/texture.hpp>
#include <p3d/context.hpp>
#include <pddi/pddi.hpp>
#include <pddi/pddienum.hpp>
#include <p3d/xbrz/xbrz.h>
#include <vector>
#include <cstring>
#include <stdint.h>
#include <stddef.h>

#ifdef RAD_ANDROID
namespace
{
    const int MAX_XBRZ_TEXTURE_DIMENSION = 4096;
    const size_t MAX_XBRZ_TEXTURE_PIXELS = (size_t)8 * 1024 * 1024;

    bool ReadArgbMip(tTexture* texture, int mip, int expectedWidth,
                     int expectedHeight, std::vector<uint32_t>& pixels)
    {
        pixels.resize((size_t)expectedWidth * (size_t)expectedHeight);
        pddiLockInfo* lock = texture->Lock(mip);
        if (lock == NULL || lock->bits == NULL)
        {
            if (lock != NULL) texture->Unlock(mip);
            return false;
        }
        if ((lock->format != PDDI_PIXEL_ARGB8888 &&
             lock->format != PDDI_PIXEL_RGB888) ||
            lock->width != expectedWidth || lock->height != expectedHeight ||
            lock->pitch == 0 ||
            (lock->pitch < 0 ? -lock->pitch : lock->pitch) < expectedWidth * 4)
        {
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
                if (lock->format == PDDI_PIXEL_RGB888)
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

tTexture* CreateXbrz2xTexture(tTexture* source)
{
    if (source == NULL || source->GetTexture() == NULL) return NULL;
    const int width = source->GetWidth();
    const int height = source->GetHeight();
    const int lastSourceMip = source->GetNumMipMaps();
    if (width <= 0 || height <= 0 ||
        width > MAX_XBRZ_TEXTURE_DIMENSION / 2 ||
        height > MAX_XBRZ_TEXTURE_DIMENSION / 2 ||
        lastSourceMip < 0 || lastSourceMip > 12)
        return NULL;

    const int targetWidth = width * 2;
    const int targetHeight = height * 2;
    const size_t targetPixels = (size_t)targetWidth * (size_t)targetHeight;
    if (targetPixels > MAX_XBRZ_TEXTURE_PIXELS) return NULL;

    tTexture* target = new tTexture;
    if (!target->Create(targetWidth, targetHeight, 32, 8,
                       lastSourceMip + 1, PDDI_TEXTYPE_RGB, PDDI_USAGE_STATIC) ||
        target->GetTexture() == NULL ||
        target->GetWidth() != targetWidth || target->GetHeight() != targetHeight)
    {
        target->Release();
        return NULL;
    }

    try
    {
        std::vector<uint32_t> sourcePixels;
        std::vector<uint32_t> scaledPixels;
        sourcePixels.resize((size_t)width * (size_t)height);
        scaledPixels.resize(targetPixels);
        if (!ReadArgbMip(source, 0, width, height, sourcePixels))
        {
            target->Release();
            return NULL;
        }

        xbrz::scale(2, sourcePixels.data(), scaledPixels.data(),
                    width, height, xbrz::ColorFormat::ARGB_UNBUFFERED);
        if (!WriteArgbMip(target, 0, targetWidth, targetHeight, scaledPixels))
        {
            target->Release();
            return NULL;
        }

        // Shift the source mip chain by one level. Output mip 1 retains the
        // original base image; output mip 2 retains source mip 1, and so on.
        for (int mip = 0; mip <= lastSourceMip; ++mip)
        {
            const int mipWidth = width >> mip;
            const int mipHeight = height >> mip;
            if (mipWidth <= 0 || mipHeight <= 0 ||
                !ReadArgbMip(source, mip, mipWidth, mipHeight, sourcePixels) ||
                !WriteArgbMip(target, mip + 1, mipWidth, mipHeight, sourcePixels))
            {
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
    return target;
}
#endif // RAD_ANDROID
