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
#include <android/log.h>
#include <ctype.h>
#include <vector>
#include <string.h>

bool IsCelShadingEnabled();

namespace
{
    struct TreeShadowAlphaState
    {
        tTexture* texture;
        std::vector< std::vector<unsigned char> > originalAlpha;
        bool sharpened;
        char name[128];
    };

    std::vector<TreeShadowAlphaState*> gTreeShadowAlphaStates;

    bool IsTreeShadowTextureName(const char* name)
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

        return strstr(lowerName, "tree") != NULL &&
               strstr(lowerName, "shadow") != NULL;
    }

    bool CaptureTreeShadowAlpha(TreeShadowAlphaState* state)
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

    bool WriteTreeShadowAlpha(TreeShadowAlphaState* state, bool sharpen)
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
            const unsigned int threshold = (maxAlpha * 60U) / 100U;
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

    void RemoveTreeShadowAlphaState(tTexture* texture)
    {
        for (std::vector<TreeShadowAlphaState*>::iterator it =
                 gTreeShadowAlphaStates.begin();
             it != gTreeShadowAlphaStates.end(); ++it)
        {
            if (*it != NULL && (*it)->texture == texture)
            {
                delete *it;
                gTreeShadowAlphaStates.erase(it);
                return;
            }
        }
    }

    void RegisterTreeShadowTexture(tTexture* texture, const char* name)
    {
        if (texture == NULL || !IsTreeShadowTextureName(name))
        {
            return;
        }

        for (size_t i = 0; i < gTreeShadowAlphaStates.size(); ++i)
        {
            if (gTreeShadowAlphaStates[i] != NULL &&
                gTreeShadowAlphaStates[i]->texture == texture)
            {
                return;
            }
        }

        TreeShadowAlphaState* state = new TreeShadowAlphaState;
        state->texture = texture;
        state->sharpened = false;
        strncpy(state->name, name, sizeof(state->name) - 1);
        state->name[sizeof(state->name) - 1] = '\0';

        if (!CaptureTreeShadowAlpha(state))
        {
            __android_log_print(ANDROID_LOG_WARN, "SHR-TreeShadowAlpha",
                "SKIP name=%s reason=unsupported-format-or-lock",
                state->name);
            delete state;
            return;
        }

        gTreeShadowAlphaStates.push_back(state);
        __android_log_print(ANDROID_LOG_INFO, "SHR-TreeShadowAlpha",
            "TRACK name=%s size=%dx%d mips=%d",
            state->name, texture->GetWidth(), texture->GetHeight(),
            texture->GetNumMipMaps() + 1);
    }
}

void UpdateTreeShadowAlphaForCelState()
{
    const bool celEnabled = IsCelShadingEnabled();

    for (size_t i = 0; i < gTreeShadowAlphaStates.size(); ++i)
    {
        TreeShadowAlphaState* state = gTreeShadowAlphaStates[i];
        if (state == NULL || state->sharpened == celEnabled)
        {
            continue;
        }

        if (WriteTreeShadowAlpha(state, celEnabled))
        {
            state->sharpened = celEnabled;
            __android_log_print(ANDROID_LOG_INFO, "SHR-TreeShadowAlpha",
                "%s name=%s thresholdPercent=60 opacityCeiling=200",
                celEnabled ? "SHARPEN" : "RESTORE", state->name);
        }
        else
        {
            __android_log_print(ANDROID_LOG_WARN, "SHR-TreeShadowAlpha",
                "UPDATE_FAILED name=%s cel=%d", state->name,
                celEnabled ? 1 : 0);
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
    RemoveTreeShadowAlphaState(this);
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
    if (texture != NULL)
    {
        texture->SetName(name);
        texture->SetPriority(priority);
#ifdef RAD_ANDROID
        RegisterTreeShadowTexture(texture, name);
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
