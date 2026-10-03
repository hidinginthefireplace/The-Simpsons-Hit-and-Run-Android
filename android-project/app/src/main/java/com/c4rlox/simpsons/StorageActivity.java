package com.c4rlox.simpsons;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
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
    private String pendingPath;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
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

    private void selectGameDataPath(String volumePath) {
        File gameDir = new File(volumePath, "Simpsons");

        if (!hasBroadStorageAccess()) {
            pendingPath = gameDir.getAbsolutePath();
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
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

        SimpsonsActivity.setGameDataPath(gameDir.getAbsolutePath());
        launchGame();
    }

    private void useInternalStorage() {
        SimpsonsActivity.setGameDataPath(null);
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

        TextView title = new TextView(this);
        title.setText("The Simpsons: Hit & Run\nGame Data Location");
        title.setTextSize(24);
        title.setGravity(Gravity.CENTER);
        root.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView help = new TextView(this);
        help.setText("\nChoose where the game data is stored. A USB drive will use the folder\n"
                + "/storage/<USB-ID>/Simpsons\n\n"
                + "Your selection is remembered for future launches.");
        help.setTextSize(16);
        help.setGravity(Gravity.CENTER);
        root.addView(help, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        String saved = SimpsonsActivity.getSavedGameDataPath();
        if (saved != null && new File(saved).isDirectory()) {
            TextView current = new TextView(this);
            current.setText("\nCurrent: " + saved);
            current.setTextSize(16);
            current.setGravity(Gravity.CENTER);
            root.addView(current);
        }

        Button internal = new Button(this);
        internal.setText("Use Internal Storage");
        internal.setFocusable(true);
        internal.setOnClickListener(v -> useInternalStorage());
        root.addView(internal, buttonParams());

        for (File volume : getUsbVolumes()) {
            Button usb = new Button(this);
            usb.setText("Use USB: " + volume.getName()
                    + "\n" + new File(volume, "Simpsons").getAbsolutePath());
            usb.setFocusable(true);
            usb.setOnClickListener(v -> selectGameDataPath(volume.getAbsolutePath()));
            root.addView(usb, buttonParams());
        }

        setContentView(root);
        internal.requestFocus();
    }

    private LinearLayout.LayoutParams buttonParams() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        p.setMargins(0, 12, 0, 0);
        return p;
    }
}
