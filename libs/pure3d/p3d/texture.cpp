/*===========================================================================
    texture.cpp

    Copyright (c)2000 Radical Entertainment, Inc.
    All rights reserved.
===========================================================================*/
#include <p3d/texture.hpp>
#include <p3d/image.hpp>
#include <p3d/imagefactory.hpp>
#include <p3d/imageconverter.hpp>
#include <p3d/utility.hpp>
#include <p3d/file.hpp>
#include <p3d/chunkfile.hpp>
#include <constants/chunks.h>
#include <constants/chunkids.hpp>
#include <constants/srrchunks.h> // For SetChunk.

#ifdef RAD_ANDROID
#include <ctype.h>
#include <vector>
#include <mutex>
#include <string.h>

bool IsCelShadingEnabled();

namespace
{
    struct SelectedShadowAlphaState
    {
        tTexture* texture;
        std::vector< std::vector<unsigned char> > originalAlpha;
        bool sharpened;
    };

    std::vector<SelectedShadowAlphaState*> gSelectedShadowAlphaStates;
    std::mutex gSelectedShadowAlphaMutex;

    bool IsSelectedShadowAlphaTextureName(const char* name)
    {
        if (name == NULL)
        {
            return false;
        }

        char lowerName[256];
        size_t i = 0;
        for (; name[i] != '\0' && i < sizeof(lowerName) - 1; ++i)
        {
            lowerName[i] = (char)tolower((unsigned char)name[i]);
        }
        lowerName[i] = '\0';

        // Keep this allow-list narrow so character/vehicle blob shadows
        // and unrelated shadow textures retain their original appearance.
        // "treeshad" covers treeshadow, treeshadowsmall, treeshad_overcast,
        // and deadtreeshad_overcast variants.
        return strstr(lowerName, "treeshad") != NULL ||
               strstr(lowerName, "beeshadow") != NULL;
    }

    bool CaptureSelectedShadowAlpha(SelectedShadowAlphaState* state)
    {
        if (state == NULL || state->texture == NULL)
        {
            return false;
        }

        const int lastMip = state->texture->GetNumMipMaps();
        if (lastMip < 0 || lastMip > 12)
        {
            return false;
        }

        state->originalAlpha.clear();
        for (int mip = 0; mip <= lastMip; ++mip)
        {
            pddiLockInfo* lock = state->texture->Lock(mip);
            if (lock == NULL || lock->bits == NULL)
            {
                if (lock != NULL)
                {
                    state->texture->Unlock(mip);
                }
                state->originalAlpha.clear();
                return false;
            }

            // Android GLES uses 32-bit ARGB for normal textured assets.
            // Compressed or unexpected formats are deliberately left alone.
            const unsigned int alphaMask = lock->rgbaMask[3];
            int alphaShift = 0;
            while (alphaShift < 32 &&
                   ((alphaMask >> alphaShift) & 1U) == 0U)
            {
                ++alphaShift;
            }

            if (lock->format != PDDI_PIXEL_ARGB8888 ||
                lock->width <= 0 || lock->height <= 0 ||
                lock->pitch == 0 || alphaMask == 0 ||
                alphaShift >= 32 || (alphaMask >> alphaShift) != 0xffU)
            {
                state->texture->Unlock(mip);
                state->originalAlpha.clear();
                return false;
            }

            std::vector<unsigned char> alpha;
            alpha.resize((size_t)lock->width * (size_t)lock->height);

            unsigned char* row = (unsigned char*)lock->bits;
            for (int y = 0; y < lock->height; ++y)
            {
                for (int x = 0; x < lock->width; ++x)
                {
                    unsigned int pixel = 0;
                    memcpy(&pixel, row + (x * 4), sizeof(pixel));
                    alpha[(size_t)y * (size_t)lock->width + (size_t)x] =
                        (unsigned char)((pixel & alphaMask) >> alphaShift);
                }
                // A negative pitch is valid on the GLES backend.
                row += lock->pitch;
            }

            state->texture->Unlock(mip);
            state->originalAlpha.push_back(alpha);
        }

        return !state->originalAlpha.empty();
    }

    bool WriteSelectedShadowAlpha(SelectedShadowAlphaState* state, bool sharpen)
    {
        if (state == NULL || state->texture == NULL ||
            state->originalAlpha.empty())
        {
            return false;
        }

        for (size_t mip = 0; mip < state->originalAlpha.size(); ++mip)
        {
            pddiLockInfo* lock = state->texture->Lock((int)mip);
            if (lock == NULL || lock->bits == NULL)
            {
                if (lock != NULL)
                {
                    state->texture->Unlock((int)mip);
                }
                return false;
            }

            const unsigned int alphaMask = lock->rgbaMask[3];
            int alphaShift = 0;
            while (alphaShift < 32 &&
                   ((alphaMask >> alphaShift) & 1U) == 0U)
            {
                ++alphaShift;
            }

            const std::vector<unsigned char>& original =
                state->originalAlpha[mip];
            if (lock->format != PDDI_PIXEL_ARGB8888 ||
                lock->width <= 0 || lock->height <= 0 ||
                lock->pitch == 0 || alphaMask == 0 ||
                alphaShift >= 32 || (alphaMask >> alphaShift) != 0xffU ||
                original.size() != (size_t)lock->width * (size_t)lock->height)
            {
                state->texture->Unlock((int)mip);
                return false;
            }

            unsigned int maxAlpha = 0;
            for (size_t i = 0; i < original.size(); ++i)
            {
                if (original[i] > maxAlpha)
                {
                    maxAlpha = original[i];
                }
            }

            // Drop faint edge pixels, then remap the remaining range to a
            // stronger yet translucent shadow. The relative cutoff keeps
            // lower-resolution mip levels from disappearing at distance.
            const unsigned int threshold = (maxAlpha * 72U) / 100U;
            unsigned char* row = (unsigned char*)lock->bits;
            for (int y = 0; y < lock->height; ++y)
            {
                for (int x = 0; x < lock->width; ++x)
                {
                    const size_t index =
                        (size_t)y * (size_t)lock->width + (size_t)x;
                    unsigned int pixel = 0;
                    memcpy(&pixel, row + (x * 4), sizeof(pixel));

                    unsigned int newAlpha = original[index];
                    if (sharpen)
                    {
                        if (maxAlpha <= threshold || newAlpha <= threshold)
                        {
                            newAlpha = 0;
                        }
                        else
                        {
                            newAlpha = ((newAlpha - threshold) * 200U) /
                                       (maxAlpha - threshold);
                            if (newAlpha > 200U)
                            {
                                newAlpha = 200U;
                            }
                        }
                    }

                    pixel = (pixel & ~alphaMask) |
                            ((newAlpha << alphaShift) & alphaMask);
                    memcpy(row + (x * 4), &pixel, sizeof(pixel));
                }
                row += lock->pitch;
            }

            state->texture->Unlock((int)mip);
        }

        return true;
    }

    void RemoveSelectedShadowAlphaState(tTexture* texture)
    {
        std::lock_guard<std::mutex> guard(gSelectedShadowAlphaMutex);
        for (std::vector<SelectedShadowAlphaState*>::iterator it =
                 gSelectedShadowAlphaStates.begin();
             it != gSelectedShadowAlphaStates.end(); ++it)
        {
            if (*it != NULL && (*it)->texture == texture)
            {
                delete *it;
                gSelectedShadowAlphaStates.erase(it);
                return;
            }
        }
    }

    void RegisterSelectedShadowTexture(tTexture* texture, const char* name)
    {
        if (texture == NULL || !IsSelectedShadowAlphaTextureName(name))
        {
            return;
        }

        std::lock_guard<std::mutex> guard(gSelectedShadowAlphaMutex);
        for (size_t i = 0; i < gSelectedShadowAlphaStates.size(); ++i)
        {
            if (gSelectedShadowAlphaStates[i] != NULL &&
                gSelectedShadowAlphaStates[i]->texture == texture)
            {
                return;
            }
        }

        SelectedShadowAlphaState* state = new SelectedShadowAlphaState;
        state->texture = texture;
        state->sharpened = false;

        if (!CaptureSelectedShadowAlpha(state))
        {
            delete state;
            return;
        }

        gSelectedShadowAlphaStates.push_back(state);
    }
}

void UpdateSelectedShadowAlphaForCelState()
{
    std::lock_guard<std::mutex> guard(gSelectedShadowAlphaMutex);
    const bool celEnabled = IsCelShadingEnabled();

    for (size_t i = 0; i < gSelectedShadowAlphaStates.size(); ++i)
    {
        SelectedShadowAlphaState* state = gSelectedShadowAlphaStates[i];
        if (state == NULL || state->sharpened == celEnabled)
        {
            continue;
        }

        if (WriteSelectedShadowAlpha(state, celEnabled))
        {
            state->sharpened = celEnabled;
        }
    }
}
#endif


tTexture::tTexture() : texture(NULL)
{
}

tTexture::~tTexture()
{
#ifdef RAD_ANDROID
    RemoveSelectedShadowAlphaState(this);
#endif
    tRefCounted::Release(texture);
}

void tTexture::SetTexture(pddiTexture* t)
{
    tRefCounted::Assign(texture, t);
}

bool tTexture::Create(int xSize, int ySize, int bpp, int alphaDepth, int nMip,
                pddiTextureType textureType,
                pddiTextureUsageHint usageHint)
    {  
        pddiTextureDesc desc;
        desc.SetSizeX(xSize);
        desc.SetSizeY(ySize);
        desc.SetBitDepth(bpp);
        desc.SetAlphaDepth(alphaDepth);
        desc.SetMipMapCount(nMip);
        desc.SetType(textureType);
        desc.SetUsage(usageHint);
        texture = p3d::device->NewTexture(&desc);

        if(!texture)
        {
            return false;
        }

        texture->AddRef();
        return true;
    }

bool tTexture::CreateVolume(int xSize, int ySize, int zSize, int bpp, int alphaDepth, int nMip,
                        pddiTextureType textureType, 
                        pddiTextureUsageHint usageHint)
    {  
        pddiTextureDesc desc;
        desc.SetSizeX(xSize);
        desc.SetSizeY(ySize);
        desc.SetSizeZ(zSize);
        desc.SetBitDepth(bpp);
        desc.SetAlphaDepth(alphaDepth);
        desc.SetMipMapCount(nMip);
        desc.SetType(textureType);
        desc.SetUsage(usageHint);
        desc.SetVolume(true);
        texture = p3d::device->NewTexture(&desc);

        if(!texture)
        {
            return false;
        }

        texture->AddRef();
        return true;
    }

//-------------------------------------------------------------------
rmt::Randomizer tSetLoader::m_Random( 0x1A30E31E );

tSetLoader::tSetLoader()
{}

tSetLoader::~tSetLoader()
{}

void tSetLoader::SetRandomSeed( unsigned NewSeed )
{
    m_Random.Seed( NewSeed );
}

tLoadStatus tSetLoader::Load( tChunkFile* InFile, tEntityStore* TempLoadingStore )
{
    if( InFile->GetCurrentID() != SRR2::ChunkID::CHUNK_SET )
    {
        return LOAD_ERROR;
    }

    // Go through the set and pick on child at random.
    char name[256];
    InFile->GetPString( name );
    int version = InFile->GetLong();
    int childCount = InFile->GetUChar();
    int childIndex = m_Random.IntRanged( 0, childCount - 1 );
    // Skip some of the children.
    for( int i = 0; i < childIndex; ++i )
    {
        InFile->BeginChunk();
        // Do nothing with the chunk.
        InFile->EndChunk();
    }
    unsigned chunkID = InFile->BeginChunk();
    tChunkHandler* chunkHandler = p3d::loadManager->GetP3DHandler()->GetHandler( chunkID );
    tLoadStatus status = LOAD_ERROR;
    if( chunkHandler )
    {
        chunkHandler->SetNameOverride( name );
        status = chunkHandler->Load( InFile, TempLoadingStore );
    }
    InFile->EndChunk();
    return status;
}

bool tSetLoader::CheckChunkID( unsigned ID )
{
    return ID == SRR2::ChunkID::CHUNK_SET;
}

unsigned int tSetLoader::GetChunkID()
{
	return SRR2::ChunkID::CHUNK_SET;
}

//-------------------------------------------------------------------
static const int TEXTURE_VERSION_V13 = 0;
static const int IMAGE_VERSION_V13 = 0;

static const int TEXTURE_VERSION = 14000;
static const int IMAGE_VERSION = 14000;
static const int VOLUME_IMAGE_VERSION = 14000;

tTextureLoader::tTextureLoader() : tSimpleChunkHandler(Pure3D::Texture::TEXTURE) 
{
    imageFactory = new tImageFactory;
    imageConverter = new tImageConverter;
};

tTextureLoader::~tTextureLoader() 
{
    delete imageFactory;
    delete imageConverter;
};

tEntity* tTextureLoader::LoadObject(tChunkFile* f, tEntityStore* store)
{
    return LoadTexture(f);
}

tTexture* tTextureLoader::LoadTexture(tChunkFile* f)
{
    char name[128];
    f->GetPString(name);

    int version = f->GetLong();
    P3DASSERT(version == TEXTURE_VERSION);   

    int width = f->GetLong();
    int height = f->GetLong();
    int bpp = f->GetLong();
    int alphaDepth = f->GetLong();
    int numMipMaps = f->GetLong();
    numMipMaps = numMipMaps ? numMipMaps - 1 : numMipMaps;
    pddiTextureType textureType = (pddiTextureType)f->GetLong();
    pddiTextureUsageHint usage = (pddiTextureUsageHint)f->GetLong();
    int priority = f->GetLong();

    imageFactory->SetDesiredDepth(bpp);
    imageFactory->SetTextureHints(alphaDepth, numMipMaps, textureType, usage);
#ifdef RAD_ANDROID
    imageFactory->SetDeferXbrzUpscale(true);
#endif

    tTexture* texture = NULL;

    int mipmap = 0;
    bool volume = false;
    bool image = false;
    while( (f->ChunksRemaining()) && (mipmap<=numMipMaps) )
    {           
        switch ( f->BeginChunk() )
        {
            case Pure3D::Texture::IMAGE:
            {
                if (!volume) 
                {
                    texture = LoadImage(f, imageFactory, texture, mipmap);
                    volume = false;
                    image = true;
                    mipmap++;
                }
                break;
            }
            case Pure3D::Texture::VOLUME_IMAGE:
            {
                if (!image)
                {
                    texture = LoadVolumeImage(f, imageFactory, texture, mipmap, numMipMaps);
                    volume = true;
                    image = false;
                    mipmap++;
                }
                break;
            }
            default:
                break;    
        }
        f->EndChunk();
    }
#ifdef RAD_ANDROID
    imageFactory->SetDeferXbrzUpscale(false);
#endif
    if (texture != NULL)
    {
        texture->SetName(name);
        texture->SetPriority(priority);
#ifdef RAD_ANDROID
        tTexture* upscaledTexture = CreateXbrz2xTexture(texture);
        if (upscaledTexture != NULL)
        {
            texture->Release();
            texture = upscaledTexture;
        }
        RegisterSelectedShadowTexture(texture, name);
#endif
    }
    return texture;
}

tTexture* tTextureLoader::LoadImage(tChunkFile* f, tImageFactory* factory, tTexture* buildTexture, int mipLevel)
{
    if(!factory)
        factory = imageFactory;

    char name[128];
    f->GetPString(name);
    int version = f->GetLong();
    P3DASSERT(version == IMAGE_VERSION);   
    int width = f->GetLong();
    int height = f->GetLong();
    int bpp = f->GetLong();
    bool palettized = f->GetLong() == 1;
    bool alpha = f->GetLong() == 1;
    unsigned format = f->GetLong();

    tTexture* texture = buildTexture;

    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE_DATA:
            {
                unsigned size = f->GetLong();

                if (texture == NULL)
                {
                    tFile* file = f->BeginInset();
                    texture = factory->ParseAsTexture( file, name, size, (tImageHandler::Format)format);
                    f->EndInset(file);
                }
                else
                {
                    tFile* file = f->BeginInset();
                    factory->ParseIntoTexture(file, texture, (tImageHandler::Format)format, mipLevel);
                    f->EndInset(file);
                }

                break;
            }

            case Pure3D::Texture::IMAGE_FILENAME:
            {
                char fileName[255];
                f->GetPString(fileName);
                if (texture == NULL)
                {
                    texture = factory->LoadAsTexture(fileName, name);
                }
                else
                {
                    factory->LoadIntoTexture(fileName, texture, mipLevel);
                }
                break;
            }

            default:
                break;
        }
        f->EndChunk();
    }

    return texture;
}

tTexture* tTextureLoader::LoadVolumeImage(tChunkFile* f, tImageFactory* factory, tTexture* buildTexture, int mipLevel, int numMipMaps)
{
    if(!factory)
        factory = imageFactory;

    char name[128];
    f->GetPString(name);
    int version = f->GetLong();
    P3DASSERT(version == VOLUME_IMAGE_VERSION);   
    int width = f->GetLong();
    int height = f->GetLong();
    int depth = f->GetLong();
    int bpp = f->GetLong();
    bool palettized = f->GetLong() == 1;
    bool alpha = f->GetLong() == 1;
    unsigned format = f->GetLong();

#ifdef RAD_XBOX
    P3DASSERT(depth>0);
    tTexture* texture = buildTexture;
    tImage** images = new tImage*[depth];
    bool autoStore = factory->GetAutoStore();
    factory->SetAutoStore(false);

    int imageNum = 0;

    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE:
            {
                if (imageNum<depth)
                {              
                    bool found = false;
                    char buf[256];
                    sprintf(buf,"%s_%i",name,imageNum);
                    while((f->ChunksRemaining()) && (!found))
                    {
                        switch(f->BeginChunk())
                        {
                            case Pure3D::Texture::IMAGE_DATA:
                            {
                                unsigned size = f->GetLong();
                                tFile* file = f->BeginInset();
                                images[imageNum] = factory->ParseAsImage(file, buf, (tImageHandler::Format)format);
                                images[imageNum]->AddRef();
                                imageNum++;
                                f->EndInset(file);
                                found = true;
                                break;
                            }

                            case Pure3D::Texture::IMAGE_FILENAME:
                            {
                                char fileName[255];
                                f->GetPString(fileName);
                                images[imageNum] = factory->LoadAsImage(fileName, buf);
                                images[imageNum]->AddRef(); 
                                imageNum++;
                                found = true;
                                break;
                            }

                            default:
                                break;
                        }
                        f->EndChunk();
                    }
                }
                break;
            }
            default:
                break;
        }
        f->EndChunk();
    }

    if (imageNum>0)
    {
        if (texture == NULL)
        {
            texture = imageConverter->ImageToVolumeTexture(images, imageNum, numMipMaps);
        }
        else
        {
            texture = imageConverter->ImageInToVolumeTexture(images, imageNum, texture, mipLevel);
        }
    }

    factory->SetAutoStore(autoStore);

    for (int i = 0; i < imageNum; i++)
    {
        images[i]->Release();
    }

    delete [] images;
    return texture;

#else
    tTexture* texture = buildTexture;
    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE:
            {
                if (texture==NULL)
                {
                    texture = LoadImage(f, factory, texture, 0);
                }
                break;
            }
            default:
                break;
        }
        f->EndChunk();
    }
    return texture;
#endif
}
