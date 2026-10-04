package com.c4rlox.simpsons;

import org.libsdl.app.SDLActivity;
import android.content.Context;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import android.view.KeyEvent;

public class SimpsonsActivity extends SDLActivity {

    private static final String GAME_DATA_PREFS = "game_data_location";
    private static final String GAME_DATA_PATH_KEY = "path";
    private static final long DOUBLE_BACK_TIMEOUT_MS = 1000;
    private long lastBackPressTime = 0;

    private static final String ANDROID_PAD_MAP =
        "a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,"
        + "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
        + "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        /*
         * SDL reads controller hints/configuration from the process environment.
         * Set these before SDLActivity starts SDL so Shield TV remotes and other
         * DPAD-only virtual devices cannot consume controller ports ahead of the
         * actual NVIDIA gamepad.
         */
        try {
            Os.setenv("SDL_TV_REMOTE_AS_JOYSTICK", "0", true);

            /*
             * NVIDIA Shield Controller (0955:7210).
             * The Android backend exposes the controller using the standard
             * Android button/axis indices, so prefer that mapping over SDL's
             * built-in mapping for the Shield GUIDs observed on this device.
             */
            Os.setenv(
                "SDL_GAMECONTROLLERCONFIG",
                "050076b85509000010720000ffff3f00,NVIDIA Shield (Android),"
                + ANDROID_PAD_MAP + "\n"
                + "050000005509000010720000ffff3f00,NVIDIA Shield (Android),"
                + ANDROID_PAD_MAP,
                true
            );
        } catch (Exception e) {
            Log.e("SimpsonsActivity", "Failed to set SDL controller environment", e);
        }

        super.onCreate(savedInstanceState);
    }

    public static void setGameDataPath(Context context, String path) {
        if (context == null) {
            return;
        }
        context.getApplicationContext().getSharedPreferences(GAME_DATA_PREFS, Context.MODE_PRIVATE)
                .edit().putString(GAME_DATA_PATH_KEY, path == null ? "" : path).apply();
    }

    public static void setGameDataPath(String path) {
        Context context = getContext();
        setGameDataPath(context, path);
    }

    public static String getSavedGameDataPath(Context context) {
        if (context == null) {
            return null;
        }
        String path = context.getApplicationContext()
                .getSharedPreferences(GAME_DATA_PREFS, Context.MODE_PRIVATE)
                .getString(GAME_DATA_PATH_KEY, "");
        return path.isEmpty() ? null : path;
    }

    public static String getSavedGameDataPath() {
        return getSavedGameDataPath(getContext());
    }

    public static String getGameDataPath() {
        String selected = getSavedGameDataPath();
        if (selected != null && new java.io.File(selected).isDirectory()) {
            return selected;
        }

        Context context = getContext();
        if (context != null) {
            java.io.File fallback = context.getExternalFilesDir(null);
            if (fallback != null) {
                return fallback.getAbsolutePath();
            }
            fallback = context.getFilesDir();
            if (fallback != null) {
                return fallback.getAbsolutePath();
            }
        }
        return null;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            // Consume both key-down and key-up so SDL never handles Back first.
            if (event.getAction() == KeyEvent.ACTION_UP) {
                long now = System.currentTimeMillis();

                if (now - lastBackPressTime <= DOUBLE_BACK_TIMEOUT_MS) {
                    finishAndRemoveTask();
                }

                lastBackPressTime = now;
            }
            return true;
        }

        return super.dispatchKeyEvent(event);
    }

}
