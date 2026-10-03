package com.c4rlox.simpsons;

import org.libsdl.app.SDLActivity;
import android.content.Context;

public class SimpsonsActivity extends SDLActivity {

    private static final String GAME_DATA_PREFS = "game_data_location";
    private static final String GAME_DATA_PATH_KEY = "path";

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

}