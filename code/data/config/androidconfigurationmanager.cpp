#include <data/config/androidconfigurationmanager.h>
#include <input/touch/touchcameracontroller.h>
#include <stdio.h>
#include <string.h>

#if defined(RAD_ANDROID)
    #include <SDL.h>
    #include <sys/stat.h>
    #include <errno.h>
    #include <unistd.h>
#endif

namespace
{
    const int CURRENT_CONFIGURATION_VERSION = 2;

#if defined(RAD_ANDROID)
    const char* CONFIGURATION_FOLDER_NAME = "Controller";
    const char* CONFIGURATION_FILE_NAME   = "Simpsons_controls.txt";
    const char* VERSION_FILE_NAME         = ".versionSimpsons_controls";
    const char* LEGACY_CONFIGURATION_FOLDER_NAME = "Simpsons_configuration";
    const char* LEGACY_CONFIGURATION_FILE_NAME = "Simpsons_configuration.txt";
    const char* LEGACY_VERSION_FILE_NAME = ".versionSimpsons_configuration";

    static bool PathExists(const char* path)
    {
        struct stat info;
        return stat(path, &info) == 0;
    }

    static void MigrateLegacyControllerPaths(const char* basePath)
    {
        char oldDirectory[512], newDirectory[512];
        char oldConfiguration[512], newConfiguration[512];
        char oldVersion[512], newVersion[512];
        snprintf(oldDirectory, sizeof(oldDirectory), "%s/%s", basePath, LEGACY_CONFIGURATION_FOLDER_NAME);
        snprintf(newDirectory, sizeof(newDirectory), "%s/%s", basePath, CONFIGURATION_FOLDER_NAME);
        snprintf(oldConfiguration, sizeof(oldConfiguration), "%s/%s", oldDirectory, LEGACY_CONFIGURATION_FILE_NAME);
        snprintf(newConfiguration, sizeof(newConfiguration), "%s/%s", newDirectory, CONFIGURATION_FILE_NAME);
        snprintf(oldVersion, sizeof(oldVersion), "%s/%s", oldDirectory, LEGACY_VERSION_FILE_NAME);
        snprintf(newVersion, sizeof(newVersion), "%s/%s", newDirectory, VERSION_FILE_NAME);

        struct stat info;
        if (stat(newDirectory, &info) != 0)
        {
            if (stat(oldDirectory, &info) == 0)
                rename(oldDirectory, newDirectory);
            else
                mkdir(newDirectory, 0777);
        }

        if (!PathExists(newConfiguration))
        {
            char moved[512];
            snprintf(moved, sizeof(moved), "%s/%s", newDirectory, LEGACY_CONFIGURATION_FILE_NAME);
            if (PathExists(moved))
                rename(moved, newConfiguration);
            else if (PathExists(oldConfiguration))
                rename(oldConfiguration, newConfiguration);
        }

        if (!PathExists(newVersion))
        {
            char moved[512];
            snprintf(moved, sizeof(moved), "%s/%s", newDirectory, LEGACY_VERSION_FILE_NAME);
            if (PathExists(moved))
                rename(moved, newVersion);
            else if (PathExists(oldVersion))
                rename(oldVersion, newVersion);
        }

        rmdir(oldDirectory);
    }
#endif
}



AndroidConfigurationManager& AndroidConfigurationManager::GetInstance()
{
    static AndroidConfigurationManager sInstance;
    return sInstance;
}

AndroidConfigurationManager* GetAndroidConfigurationManager()
{
    return &AndroidConfigurationManager::GetInstance();
}

AndroidConfigurationManager::AndroidConfigurationManager()
:
    m_initialized( false ),
    m_invertCamera( false ),
    m_gamepadVibration( true ),
    m_phoneVibration( false ),
    m_dirty( false )
{
#if defined(RAD_ANDROID)
    m_directoryPath[ 0 ] = '\0';
    m_configurationFilePath[ 0 ] = '\0';
    m_versionFilePath[ 0 ] = '\0';
#endif
}

void AndroidConfigurationManager::SetDefaults()
{
    m_invertCamera = false;
    m_gamepadVibration = true;
    m_phoneVibration = false;
    m_dirty = false;
}

void AndroidConfigurationManager::Initialize()
{
    if( m_initialized )
    {
        return;
    }

    this->SetDefaults();

#if defined(RAD_ANDROID)
    if( !this->BuildPaths() )
    {
        m_initialized = true;
        return;
    }

    this->EnsureDirectory();

    const bool loadedConfiguration = this->LoadConfigurationFile();
    const int loadedVersion = this->LoadVersionFile();

    //
    // If the file does not exist, or the version is old/missing,
    // rewrite the configuration while preserving any values already loaded.
    //
    if( !loadedConfiguration || loadedVersion != CURRENT_CONFIGURATION_VERSION )
    {
        this->WriteConfigurationFile();
        this->WriteVersionFile();
    }
#endif

    m_initialized = true;
}

void AndroidConfigurationManager::Reload()
{
    this->SetDefaults();

#if defined(RAD_ANDROID)
    if( !this->BuildPaths() )
    {
        return;
    }

    this->LoadConfigurationFile();
#endif
}

void AndroidConfigurationManager::Save()
{
#if defined(RAD_ANDROID)
    if( !this->BuildPaths() )
    {
        return;
    }

    this->EnsureDirectory();
    this->WriteConfigurationFile();
    this->WriteVersionFile();
    m_dirty=false;
#endif
}

void AndroidConfigurationManager::SaveIfDirty()
{
#if defined(RAD_ANDROID)
    if( !m_dirty )
    {
        return;
    }

    this->Save();
#endif
}

bool AndroidConfigurationManager::IsInvertCameraEnabled() const
{
    return m_invertCamera;
}

bool AndroidConfigurationManager::IsGamepadVibrationEnabled() const
{
    return m_gamepadVibration;
}

bool AndroidConfigurationManager::IsPhoneVibrationEnabled() const
{
    return m_phoneVibration;
}

void AndroidConfigurationManager::SetInvertCameraEnabled( bool enabled )
{
    if( m_invertCamera == enabled )
    {
        return;
    }

    m_invertCamera = enabled;
    m_dirty = true;
}

void AndroidConfigurationManager::SetGamepadVibrationEnabled( bool enabled )
{
    if( m_gamepadVibration == enabled )
    {
        return;
    }

    m_gamepadVibration = enabled;
    m_dirty = true;
}

void AndroidConfigurationManager::SetPhoneVibrationEnabled( bool enabled )
{
    m_phoneVibration = enabled;
}

#if defined(RAD_ANDROID)

bool AndroidConfigurationManager::BuildPaths()
{
    // Simpsons configuration belongs in the app's primary Android
    // external-files directory under /Android/data/<package>/files.
    // Do not use SDL_AndroidGetExternalStoragePath(), because this project
    // overrides that API for the selectable USB game-data location.
    const char* basePath = SDL_AndroidGetApplicationExternalFilesPath();

    if( basePath == NULL || basePath[ 0 ] == '\0' )
    {
        return false;
    }

    MigrateLegacyControllerPaths( basePath );

    snprintf( m_directoryPath,
              sizeof( m_directoryPath ),
              "%s/%s",
              basePath,
              CONFIGURATION_FOLDER_NAME );

    snprintf( m_configurationFilePath,
              sizeof( m_configurationFilePath ),
              "%s/%s",
              m_directoryPath,
              CONFIGURATION_FILE_NAME );

    snprintf( m_versionFilePath,
              sizeof( m_versionFilePath ),
              "%s/%s",
              m_directoryPath,
              VERSION_FILE_NAME );

    return true;
}

bool AndroidConfigurationManager::EnsureDirectory()
{
    if( m_directoryPath[ 0 ] == '\0' )
    {
        return false;
    }

    const int result = mkdir( m_directoryPath, 0777 );

    if( result == 0 )
    {
        return true;
    }

    return errno == EEXIST;
}

bool AndroidConfigurationManager::LoadConfigurationFile()
{
    FILE* file = fopen( m_configurationFilePath, "r" );

    if( file == NULL )
    {
        return false;
    }

    char line[ 256 ];

    while( fgets( line, sizeof( line ), file ) != NULL )
    {
        this->TrimLine( line );

        if( line[ 0 ] == '\0' || line[ 0 ] == '#' )
        {
            continue;
        }

        this->ParseLine( line );
    }

    fclose( file );

    return true;
}

int AndroidConfigurationManager::LoadVersionFile()
{
    FILE* file = fopen( m_versionFilePath, "r" );

    if( file == NULL )
    {
        return 0;
    }

    int version = 0;
    fscanf( file, "%d", &version );

    fclose( file );

    return version;
}

void AndroidConfigurationManager::WriteConfigurationFile()
{
    FILE* file = fopen( m_configurationFilePath, "w" );

    if( file == NULL )
    {
        return;
    }

    fprintf( file, "# Simpsons Hit & Run - Revamped! [Controller Configuration]\n" );
    fprintf( file, "# Inverted Camera and Controller Vibration toggles.\n" );
    fprintf( file, "# Change with true/false or 1/0 values.\n" );
    fprintf( file, "# You can also use the in-game settings toggle.\n" );
    fprintf( file, "# Changes to this file will be reflected upon restart.\n" );
    fprintf( file, "\n" );

    fprintf( file, "invert_camera=%s\n", m_invertCamera ? "true" : "false" );
    fprintf( file, "gamepad_vibration=%s\n", m_gamepadVibration ? "true" : "false" );

    fclose( file );
}

void AndroidConfigurationManager::WriteVersionFile()
{
    FILE* file = fopen( m_versionFilePath, "w" );

    if( file == NULL )
    {
        return;
    }

    fprintf( file, "%d\n", CURRENT_CONFIGURATION_VERSION );

    fclose( file );
}

bool AndroidConfigurationManager::ParseBool( const char* value, bool defaultValue ) const
{
    if( value == NULL )
    {
        return defaultValue;
    }

    if( strcmp( value, "true" ) == 0 ||
        strcmp( value, "1" ) == 0 ||
        strcmp( value, "on" ) == 0 ||
        strcmp( value, "yes" ) == 0 )
    {
        return true;
    }

    if( strcmp( value, "false" ) == 0 ||
        strcmp( value, "0" ) == 0 ||
        strcmp( value, "off" ) == 0 ||
        strcmp( value, "no" ) == 0 )
    {
        return false;
    }

    return defaultValue;
}

void AndroidConfigurationManager::TrimLine( char* text ) const
{
    if( text == NULL )
    {
        return;
    }

    int length = static_cast<int>( strlen( text ) );

    while( length > 0 &&
           ( text[ length - 1 ] == '\n' ||
             text[ length - 1 ] == '\r' ||
             text[ length - 1 ] == ' '  ||
             text[ length - 1 ] == '\t' ) )
    {
        text[ length - 1 ] = '\0';
        length--;
    }

    char* start = text;

    while( *start == ' ' || *start == '\t' )
    {
        start++;
    }

    if( start != text )
    {
        memmove( text, start, strlen( start ) + 1 );
    }
}

void AndroidConfigurationManager::ParseLine( const char* line )
{
    char buffer[ 256 ];
    strncpy( buffer, line, sizeof( buffer ) );
    buffer[ sizeof( buffer ) - 1 ] = '\0';

    char* separator = strchr( buffer, '=' );

    if( separator == NULL )
    {
        return;
    }

    *separator = '\0';

    char* key = buffer;
    char* value = separator + 1;

    this->TrimLine( key );
    this->TrimLine( value );

    if( strcmp( key, "invert_camera" ) == 0 )
    {
        m_invertCamera = this->ParseBool( value, m_invertCamera );
    }
    else if( strcmp( key, "gamepad_vibration" ) == 0 )
    {
        m_gamepadVibration = this->ParseBool( value, m_gamepadVibration );
    }
    else if( strcmp( key, "phone_vibration" ) == 0 )
    {
        m_phoneVibration = this->ParseBool( value, m_phoneVibration );
    }
}

#endif // RAD_ANDROID