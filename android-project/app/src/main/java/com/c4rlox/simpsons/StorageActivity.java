package com.c4rlox.simpsons;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
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
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

public class StorageActivity extends Activity {
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
                    .setTitle("Removable storage unavailable")
                    .setMessage("Could not create:\n" + gameDir.getAbsolutePath()
                            + "\n\nCheck that the removable storage is writable.")
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

    private List<File> getRemovableVolumes() {
        List<File> result = new ArrayList<>();
        String primaryStoragePath = Environment.getExternalStorageDirectory().getAbsolutePath();

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

                // Shield TV can expose /storage/emulated/0 as a removable volume.
                // It is still the primary internal shared storage, so never list
                // it as a USB option.
                if (directory != null
                        && directory.isDirectory()
                        && !directory.getAbsolutePath().equals(primaryStoragePath)) {
                    result.add(directory);
                }
            }
        }
        return result;
    }

    private void showStorageScreen() {
        FrameLayout root = new FrameLayout(this);

        ImageView background = new ImageView(this);
        background.setScaleType(ImageView.ScaleType.CENTER_CROP);
        try (InputStream input = getAssets().open("storage_screen_background.webp")) {
            Bitmap bitmap = BitmapFactory.decodeStream(input);
            background.setImageBitmap(bitmap);
        } catch (IOException | RuntimeException e) {
            background.setBackgroundColor(SKY_BLUE);
        }
        root.addView(background, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER_HORIZONTAL);
        int sidePad = dp(24);
        int topPad = dp(28);
        content.setPadding(sidePad, topPad, sidePad, dp(28));

        TextView title = new TextView(this);
        title.setText("The Simpsons: Hit & Run\nGame Data Location");
        title.setTextSize(29);
        title.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);
        title.setTextColor(YELLOW);
        title.setGravity(Gravity.CENTER);
        title.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
        content.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView help = new TextView(this);
        help.setText("\nFirst time setup\n\nChoose where the game data is stored");
        help.setTextSize(20);
        help.setTextColor(Color.WHITE);
        help.setGravity(Gravity.CENTER);
        help.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
        content.addView(help, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        String saved = SimpsonsActivity.getSavedGameDataPath(this);
        if (saved != null && new File(saved).isDirectory()) {
            TextView current = new TextView(this);
            current.setText("\nCurrent: " + saved);
            current.setTextSize(17);
            current.setTextColor(YELLOW);
            current.setGravity(Gravity.CENTER);
            current.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
            content.addView(current, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT));
        }

        Button internal = new Button(this);
        styleButton(internal);
        internal.setText("Use Internal Storage");
        internal.setFocusable(true);
        internal.setOnClickListener(v -> useInternalStorage());
        content.addView(internal, buttonParams());

        for (File volume : getRemovableVolumes()) {
            Button removable = new Button(this);
            styleButton(removable);
            removable.setText("Use Removable Storage: " + volume.getName()
                    + "\n" + getGameDataDirectory(volume.getAbsolutePath()).getAbsolutePath());
            removable.setFocusable(true);
            removable.setOnClickListener(v -> selectGameDataPath(volume.getAbsolutePath()));
            content.addView(removable, buttonParams());
        }

        scroll.addView(content, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        root.addView(scroll, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        setContentView(root);
        internal.requestFocus();
    }

    private void styleButton(Button button) {
        button.setTextColor(DARK_BLUE);
        button.setAllCaps(false);
        button.setTextSize(19);
        button.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);
        button.setGravity(Gravity.CENTER);
        button.setMinHeight(dp(82));
        button.setPadding(dp(20), dp(14), dp(20), dp(14));

        GradientDrawable normal = new GradientDrawable();
        normal.setColor(YELLOW);
        normal.setCornerRadius(dp(14));

        GradientDrawable focused = new GradientDrawable();
        focused.setColor(DARK_BLUE);
        focused.setCornerRadius(dp(14));

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
        p.setMargins(0, dp(10), 0, 0);
        return p;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
