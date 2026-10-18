#include <input/touch/touchinputmodemanager.h>

TouchInputModeManager& TouchInputModeManager::GetInstance()
{
    static TouchInputModeManager sInstance;
    return sInstance;
}

TouchInputModeManager::TouchInputModeManager()
    :
    mCurrentMode( TOUCH_INPUT_MODE_TOUCH ),
    mGamepadCandidate(false),
    mGamepadConnected( false ),
    mTouchHudEnabled( true ),
    mGamepadInputThreshold( 0.20f ),
    mTimeMs( 0 ),
    mLastTouchInputTimeMs( 0 ),
    mLastGamepadInputTimeMs( 0 )
{
}

TouchInputModeManager::~TouchInputModeManager()
{
}

void TouchInputModeManager::Reset()
{
    mCurrentMode = TOUCH_INPUT_MODE_TOUCH;
    mGamepadCandidate = false;
    mGamepadConnected = false;
    mTouchHudEnabled = true;
    mGamepadInputThreshold = 0.20f;
    mTimeMs = 0;
    mLastTouchInputTimeMs = 0;
    mLastGamepadInputTimeMs = 0;
}

void TouchInputModeManager::Update( unsigned int elapsedMs )
{
    mTimeMs += elapsedMs;
}

void TouchInputModeManager::NotifyTouchInput()
{
    mLastTouchInputTimeMs = mTimeMs;

    /*
     * A direct touch is an explicit request to use the touchscreen again.
     * A confirmed controller remains tracked, but its next real input will
     * switch the active mode back to Gamepad and hide the touch HUD.
     */
    mCurrentMode = TOUCH_INPUT_MODE_TOUCH;
}

void TouchInputModeManager::NotifyGamepadCandidateConnected()
{
    /*
     * SDL/Android detected something that may be a physical gamepad.
     *
     * Important:
     * A candidate must NOT hide the touch HUD.
     * A candidate must NOT block touch input.
     * A candidate must NOT enable rumble.
     *
     * Only real input from the physical controller confirms the gamepad.
     */
    mGamepadCandidate = true;

    if ( !mGamepadConnected )
    {
        mCurrentMode = TOUCH_INPUT_MODE_TOUCH;
    }
}

void TouchInputModeManager::NotifyGamepadConnected()
{
    /*
     * This now means:
     * A physical gamepad has been CONFIRMED by real input.
     *
     * Keep selecting Gamepad mode whenever this is called, not just on the
     * first connection transition. That lets real controller input take back
     * priority after the user has temporarily touched the screen.
     */
    mGamepadCandidate = true;
    mGamepadConnected = true;
    mCurrentMode = TOUCH_INPUT_MODE_GAMEPAD;
    mLastGamepadInputTimeMs = mTimeMs;
}

void TouchInputModeManager::NotifyGamepadDisconnected()
{
    mGamepadCandidate = false;
    mGamepadConnected = false;

    // On Android, if the external gamepad is gone, touch should become the
    // default available input method again.
    mCurrentMode = TOUCH_INPUT_MODE_TOUCH;
}

void TouchInputModeManager::NotifyGamepadInput( float magnitude )
{
    if ( magnitude < 0.0f )
    {
        magnitude = -magnitude;
    }

    if ( magnitude < mGamepadInputThreshold )
    {
        return;
    }

    // Nos confirma una pulsación real del mando
    // por lo que bloqueamos el tactil hasta que se nos envie que dicho mando se desconecte
    NotifyGamepadConnected();
}

void TouchInputModeManager::ForceMode( TouchInputMode mode )
{
    // mmm... Esto es una capa extra de seguridad  la cual no se si es estrictamente necesaria...
    // la dejo por el momento luego pruebo el sistema sin este if de seguridad
    // temo porque hay muchos telefonos distintos
    if ( mGamepadConnected && mode == TOUCH_INPUT_MODE_TOUCH )
    {
        return;
    }
    mCurrentMode = mode;
}