package com.c4rlox.simpsons;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class StorageActivity extends Activity {
    private static final int REQUEST_STORAGE_PERMISSION = 42;
    private static final int SKY_BLUE = Color.rgb(135, 206, 235);
    private static final int DARK_BLUE = Color.rgb(23, 59, 108);
    private static final int YELLOW = Color.rgb(212, 176, 0);
    private String pendingPath;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        String saved = SimpsonsActivity.getSavedGameDataPath(this);
        if (saved != null && new File(saved).isDirectory()) {
            if (hasBroadStorageAccess()) {
                launchGame();
                return;
            }
            pendingPath = saved;
            requestBroadStorageAccess();
            return;
        }

        showStorageScreen();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (pendingPath != null && hasBroadStorageAccess()) {
            String path = pendingPath;
            pendingPath = null;
            selectGameDataPath(path);
        }
    }

    private boolean hasBroadStorageAccess() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }

    private void requestBroadStorageAccess() {
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    private File getGameDataDirectory(String volumePath) {
        File volumeDir = new File(volumePath);
        // Some Android devices expose a removable volume whose mount point is
        // already named "Simpsons". Do not create /Simpsons/Simpsons in that case.
        if ("Simpsons".equalsIgnoreCase(volumeDir.getName())) {
            return volumeDir;
        }
        return new File(volumeDir, "Simpsons");
    }

    private void selectGameDataPath(String volumePath) {
        File gameDir = getGameDataDirectory(volumePath);

        if (!hasBroadStorageAccess()) {
            pendingPath = gameDir.getAbsolutePath();
            requestBroadStorageAccess();
            return;
        }

        if (!gameDir.exists() && !gameDir.mkdirs()) {
            new AlertDialog.Builder(this)
                    .setTitle("USB folder unavailable")
                    .setMessage("Could not create:\n" + gameDir.getAbsolutePath()
                            + "\n\nCheck that the USB drive is writable.")
                    .setPositiveButton("OK", null)
                    .show();
            return;
        }

        SimpsonsActivity.setGameDataPath(this, gameDir.getAbsolutePath());
        launchGame();
    }

    private void useInternalStorage() {
        SimpsonsActivity.setGameDataPath(this, null);
        launchGame();
    }

    private void launchGame() {
        startActivity(new Intent(this, SimpsonsActivity.class));
        finish();
    }

    private List<File> getUsbVolumes() {
        List<File> result = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= 24) {
            StorageManager manager = (StorageManager) getSystemService(Context.STORAGE_SERVICE);
            for (StorageVolume volume : manager.getStorageVolumes()) {
                if (!volume.isRemovable()) {
                    continue;
                }
                File directory = null;
                if (Build.VERSION.SDK_INT >= 30) {
                    directory = volume.getDirectory();
                }
                if (directory != null && directory.isDirectory()) {
                    result.add(directory);
                }
            }
        }
        return result;
    }

    private void showStorageScreen() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = (int) (32 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);
        root.setBackgroundColor(SKY_BLUE);

        TextView title = new TextView(this);
        title.setText("The Simpsons: Hit & Run\nGame Data Location");
        title.setTextSize(24);
        title.setTextColor(YELLOW);
        title.setGravity(Gravity.CENTER);
        root.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView help = new TextView(this);
        help.setText("\nChoose where the game data is stored. A USB drive will use the folder\n"
                + "/storage/<USB-ID>/Simpsons\n\n"
                + "Your selection is remembered for future launches.");
        help.setTextSize(16);
        help.setTextColor(Color.WHITE);
        help.setGravity(Gravity.CENTER);
        root.addView(help, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        String saved = SimpsonsActivity.getSavedGameDataPath(this);
        if (saved != null && new File(saved).isDirectory()) {
            TextView current = new TextView(this);
            current.setText("\nCurrent: " + saved);
            current.setTextSize(16);
            current.setTextColor(YELLOW);
            current.setGravity(Gravity.CENTER);
            root.addView(current);
        }

        Button internal = new Button(this);
        styleButton(internal);
        internal.setText("Use Internal Storage");
        internal.setFocusable(true);
        internal.setOnClickListener(v -> useInternalStorage());
        root.addView(internal, buttonParams());

        for (File volume : getUsbVolumes()) {
            Button usb = new Button(this);
            styleButton(usb);
            usb.setText("Use USB: " + volume.getName()
                    + "\n" + getGameDataDirectory(volume.getAbsolutePath()).getAbsolutePath());
            usb.setFocusable(true);
            usb.setOnClickListener(v -> selectGameDataPath(volume.getAbsolutePath()));
            root.addView(usb, buttonParams());
        }

        setContentView(root);
        internal.requestFocus();
    }

    private void styleButton(Button button) {
        button.setTextColor(DARK_BLUE);
        button.setAllCaps(false);
        button.setTextSize(16);
        button.setPadding(20, 16, 20, 16);

        GradientDrawable normal = new GradientDrawable();
        normal.setColor(YELLOW);
        normal.setCornerRadius(12 * getResources().getDisplayMetrics().density);

        GradientDrawable focused = new GradientDrawable();
        focused.setColor(DARK_BLUE);
        focused.setCornerRadius(12 * getResources().getDisplayMetrics().density);

        StateListDrawable states = new StateListDrawable();
        states.addState(new int[] { android.R.attr.state_focused }, focused);
        states.addState(new int[] { android.R.attr.state_pressed }, focused);
        states.addState(new int[] {}, normal);

        button.setBackground(states);

        button.setOnFocusChangeListener((v, hasFocus) ->
                button.setTextColor(hasFocus ? Color.WHITE : DARK_BLUE));
    }

    private LinearLayout.LayoutParams buttonParams() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        p.setMargins(0, 12, 0, 0);
        return p;
    }
}
