        }
    }

    if (JoystickByDeviceId(device_id) != NULL || !name) {
        goto done;
    }

#ifdef SDL_JOYSTICK_HIDAPI
    /*
     * Android already has a complete Java InputDevice path for gamepads.
     * Some Android/Shield TV Bluetooth controllers are also visible through
     * HIDAPI, but HIDAPI may not provide the same Android key/motion events
     * that SDLControllerManager receives. If we let HIDAPI claim the device
     * here, Android_OnPadDown/Up and Android_OnJoy can be left without an
     * SDL joystick object, producing controller input that wakes the UI but
     * never reaches the game. Keep the Android joystick registration in
     * charge of Android InputDevice devices.
     */
#endif

#ifdef DEBUG_JOYSTICK
    SDL_Log("Joystick: %s, descriptor %s, vendor = 0x%.4x, product = 0x%.4x, %d axes, %d hats\\n", name, desc, vendor_id, product_id, naxes, nhats);
#endif

    if (nhats > 0) {
        /* Hat is translated into DPAD buttons */
        button_mask |= ((1 << SDL_CONTROLLER_BUTTON_DPAD_UP) |
                        (1 << SDL_CONTROLLER_BUTTON_DPAD_DOWN) |
                        (1 << SDL_CONTROLLER_BUTTON_DPAD_LEFT) |
                        (1 << SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        nhats = 0;
    }

    guid = SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_BLUETOOTH, vendor_id, product_id, 0, NULL, desc, 0, 0);

    /* Update the GUID with capability bits */
    {
        Uint16 *guid16 = (Uint16 *)guid.data;
        guid16[6] = SDL_SwapLE16(button_mask);
        guid16[7] = SDL_SwapLE16(axis_mask);
    }

    item = (SDL_joylist_item *)SDL_malloc(sizeof(SDL_joylist_item));
    if (!item) {
        goto done;
    }

    SDL_zerop(item);
    item->guid = guid;
    item->device_id = device_id;
    item->name = SDL_CreateJoystickName(vendor_id, product_id, NULL, name);
    if (!item->name) {
        SDL_free(item);
        goto done;
    }

    item->is_accelerometer = is_accelerometer;