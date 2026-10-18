package com.sevarg.simpsons;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.UiModeManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.Settings;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.Gravity;
import android.view.View;
import android.view.animation.AccelerateDecelerateInterpolator;
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
    private static final int PINK = Color.rgb(255, 105, 180);
    private static final int YELLOW = Color.rgb(212, 176, 0);

    // Prevent repeatedly opening Android's all-files-access settings after a user
    // has declined the automatic first-launch scan. Manual storage selection can
    // still request access using the existing flow.
    private static final String AUTO_SCAN_PREFERENCES = "game_data_auto_scan";
    private static final String AUTO_SCAN_PERMISSION_REQUESTED_KEY = "all_files_permission_requested";

    // Files explicitly requested during the PAL startup sequence.
    // This catches common incomplete/incorrect installs before native startup.
    private static final String[] REQUIRED_STARTUP_FILES = {
            "art/frontend/scrooby/language.p3d",
            "art/frontend/scrooby/bootup.p3d",
            "art/frontend/scrooby/backend.p3d",
            "art/frontend/scrooby/frontend.p3d",
            "art/cars/common.p3d",
            "art/cars/huskA.p3d",
            "art/phonecamera.p3d",
            "art/cards.p3d",
            "art/wrench.p3d",
            "art/missions/generic/missgen.p3d"
    };

    private String pendingPath;
    private boolean televisionLayout;
    private boolean portraitLayout;
    private boolean pendingAutomaticScanAfterPermission;
    private boolean automaticScanPermissionSettingsWasOpened;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        televisionLayout = isTelevisionDevice();
        setRequestedOrientation(televisionLayout
                ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                : ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);

        String saved = SimpsonsActivity.getSavedGameDataPath(this);
        if (saved != null) {
            File savedDirectory = new File(saved);
            if (hasBroadStorageAccess()) {
                if (hasRequiredGameData(savedDirectory)) {
                    launchGame();
                    return;
                }

                // The previously used directory may have been moved or deleted.
                // Search the other known locations before asking the user again.
                if (tryAutomaticGameDataDetection()) {
                    return;
                }

                showStorageScreen();
                showGameDataNotFound(savedDirectory);
                return;
            }

            if (savedDirectory.isDirectory()) {
                pendingPath = saved;
                requestBroadStorageAccess();
                return;
            }

            // A saved removable-storage path may no longer be mounted. Check
            // app-owned internal storage first, then request shared-storage
            // access once if it is needed to scan the other locations.
            if (tryAutomaticGameDataDetection()) {
                return;
            }
            if (requestAutomaticScanPermissionIfNeeded()) {
                return;
            }

            showStorageScreen();
            showGameDataNotFound(savedDirectory);
            return;
        }

        // Fresh install/reinstall: discover an existing game-data folder before
        // showing the first-time location selector.
        if (tryAutomaticGameDataDetection()) {
            return;
        }
        if (requestAutomaticScanPermissionIfNeeded()) {
            return;
        }

        showStorageScreen();
    }

    @Override
    protected void onPause() {
        // startActivity() can occur from onCreate(), before this Activity's
        // first resume callback. Only process the automatic scan on the resume
        // that follows the actual trip to Android Settings.
        if (pendingAutomaticScanAfterPermission) {
            automaticScanPermissionSettingsWasOpened = true;
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();

        if (pendingAutomaticScanAfterPermission
                && automaticScanPermissionSettingsWasOpened) {
            pendingAutomaticScanAfterPermission = false;
            automaticScanPermissionSettingsWasOpened = false;
            if (hasBroadStorageAccess() && tryAutomaticGameDataDetection()) {
                return;
            }

            // Permission was declined or no valid installation was found.
            // Keep the existing manual selector as the safe fallback.
            showStorageScreen();
            return;
        }

        if (pendingPath != null && hasBroadStorageAccess()) {
            String path = pendingPath;
            pendingPath = null;

            File gameDirectory = new File(path);
            if (!gameDirectory.exists() && !gameDirectory.mkdirs()) {
                new AlertDialog.Builder(this)
                        .setTitle("Storage folder unavailable")
                        .setMessage("Could not create:\n" + gameDirectory.getAbsolutePath()
                                + "\n\nCheck that the selected storage is available and writable.")
                        .setPositiveButton("OK", null)
                        .show();
                showStorageScreen();
                return;
            }

            if (hasRequiredGameData(gameDirectory)) {
                SimpsonsActivity.setGameDataPath(this, gameDirectory.getAbsolutePath());
                launchGame();
            } else if (!tryAutomaticGameDataDetection()) {
                showStorageScreen();
                showGameDataNotFound(gameDirectory);
            }
        }
    }

    private boolean isTelevisionDevice() {
        UiModeManager uiModeManager =
                (UiModeManager) getSystemService(Context.UI_MODE_SERVICE);
        if (uiModeManager != null
                && uiModeManager.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION) {
            return true;
        }
        return getPackageManager().hasSystemFeature(PackageManager.FEATURE_LEANBACK);
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

    /**
     * Requests the existing all-files access permission once when automatic
     * discovery needs to inspect shared internal/removable storage. On return,
     * onResume() retries the scan. If the user declines, manual selection stays
     * available and this launch does not trap them in a permission loop.
     */
    private boolean requestAutomaticScanPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT < 30 || hasBroadStorageAccess()) {
            return false;
        }

        if (getSharedPreferences(AUTO_SCAN_PREFERENCES, MODE_PRIVATE)
                .getBoolean(AUTO_SCAN_PERMISSION_REQUESTED_KEY, false)) {
            return false;
        }

        getSharedPreferences(AUTO_SCAN_PREFERENCES, MODE_PRIVATE)
                .edit()
                .putBoolean(AUTO_SCAN_PERMISSION_REQUESTED_KEY, true)
                .apply();

        pendingAutomaticScanAfterPermission = true;
        requestBroadStorageAccess();
        return true;
    }

    /**
     * Searches the known game-data roots in priority order: app-owned internal,
     * shared internal (/Simpsons), then mounted removable volumes (/Simpsons).
     * A candidate is accepted only if it passes the same startup-file checks as
     * manual selection. A null saved path represents the app-owned data folder.
     */
    private boolean tryAutomaticGameDataDetection() {
        String internalPath = SimpsonsActivity.getInternalGameDataPath(this);
        File internalDirectory = internalPath != null ? new File(internalPath) : null;
        if (internalDirectory != null && hasRequiredGameData(internalDirectory)) {
            SimpsonsActivity.setGameDataPath(this, null);
            launchGame();
            return true;
        }

        // Shared and removable storage cannot be reliably searched on Android
        // 11+ until broad storage access has been granted.
        if (!hasBroadStorageAccess()) {
            return false;
        }

        File sharedInternalDirectory = getSharedInternalGameDataDirectory();
        if (hasRequiredGameData(sharedInternalDirectory)) {
            SimpsonsActivity.setGameDataPath(this, sharedInternalDirectory.getAbsolutePath());
            launchGame();
            return true;
        }

        for (File volume : getRemovableVolumes()) {
            File candidate = getGameDataDirectory(volume.getAbsolutePath());
            if (isSameDirectory(candidate, internalDirectory)
                    || isSameDirectory(candidate, sharedInternalDirectory)) {
                continue;
            }
            if (hasRequiredGameData(candidate)) {
                SimpsonsActivity.setGameDataPath(this, candidate.getAbsolutePath());
                launchGame();
                return true;
            }
        }

        return false;
    }

    private boolean isSameDirectory(File first, File second) {
        if (first == null || second == null) {
            return false;
        }
        try {
            return first.getCanonicalFile().equals(second.getCanonicalFile());
        } catch (IOException e) {
            return first.getAbsolutePath().equals(second.getAbsolutePath());
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

        if (!hasRequiredGameData(gameDir)) {
            showGameDataNotFound(gameDir);
            return;
        }

        // Do not persist a location until it has passed game-data validation.
        SimpsonsActivity.setGameDataPath(this, gameDir.getAbsolutePath());
        launchGame();
    }

    private void useInternalStorage() {
        String internalPath = SimpsonsActivity.getInternalGameDataPath(this);
        File internalDirectory = internalPath != null ? new File(internalPath) : null;
        if (!hasRequiredGameData(internalDirectory)) {
            showGameDataNotFound(internalDirectory);
            return;
        }

        // A null saved path tells the game to use its app-owned files directory.
        SimpsonsActivity.setGameDataPath(this, null);
        launchGame();
    }

    private File getSharedInternalGameDataDirectory() {
        return getGameDataDirectory(
                Environment.getExternalStorageDirectory().getAbsolutePath());
    }

    private void useSharedInternalStorage() {
        File gameDirectory = getSharedInternalGameDataDirectory();

        // Shared storage is outside the app sandbox on Android 11+ and may
        // require the existing all-files-access flow before it can be checked.
        if (!hasBroadStorageAccess()) {
            pendingPath = gameDirectory.getAbsolutePath();
            requestBroadStorageAccess();
            return;
        }

        if (!gameDirectory.exists() && !gameDirectory.mkdirs()) {
            new AlertDialog.Builder(this)
                    .setTitle("Internal storage unavailable")
                    .setMessage("Could not create:\n" + gameDirectory.getAbsolutePath()
                            + "\n\nCheck that internal shared storage is writable.")
                    .setPositiveButton("OK", null)
                    .show();
            return;
        }

        if (!hasRequiredGameData(gameDirectory)) {
            showGameDataNotFound(gameDirectory);
            return;
        }

        // Store the shared-storage path explicitly; this is separate from the
        // app-owned /Android/data/.../files option above.
        SimpsonsActivity.setGameDataPath(this, gameDirectory.getAbsolutePath());
        launchGame();
    }

    private List<String> getMissingRequiredGameFiles(File gameDirectory) {
        List<String> missingFiles = new ArrayList<>();

        if (gameDirectory == null || !gameDirectory.isDirectory()) {
            missingFiles.add("(selected location is unavailable or is not a folder)");
            return missingFiles;
        }

        for (String relativePath : REQUIRED_STARTUP_FILES) {
            File requiredFile = new File(gameDirectory, relativePath);
            if (!requiredFile.isFile() || !requiredFile.canRead()) {
                missingFiles.add(relativePath);
            }
        }

        return missingFiles;
    }

    private boolean hasRequiredGameData(File gameDirectory) {
        return getMissingRequiredGameFiles(gameDirectory).isEmpty();
    }

    private void showGameDataNotFound(File gameDirectory) {
        String selectedPath = gameDirectory != null
                ? gameDirectory.getAbsolutePath()
                : "(internal game-data directory unavailable)";
        List<String> missingFiles = getMissingRequiredGameFiles(gameDirectory);
        StringBuilder message = new StringBuilder();

        if (gameDirectory == null || !gameDirectory.isDirectory()) {
            message.append("The selected game-data location is unavailable or is not a folder.");
        } else {
            message.append("The selected folder is missing required startup files, or some files cannot be read.");
            message.append("\n\nMissing or unreadable files:");
            for (String missingFile : missingFiles) {
                message.append("\n• ").append(missingFile);
            }
        }

        message.append("\n\nSelected folder:\n").append(selectedPath);
        message.append("\n\nCopy the complete game-data folder from the official PC release to this location. ");
        message.append("Choose the folder that directly contains the \"art\" directory, not a parent folder ");
        message.append("or an extra nested folder. Do not use assets from the leaked source-code build; ");
        message.append("they are not compatible with the release port.");
        message.append("\n\nThese checks cover known startup dependencies. If the installation is damaged, ");
        message.append("incompatible, or missing other assets, the game may still report additional missing files.");

        new AlertDialog.Builder(this)
                .setTitle("Game data not found")
                .setMessage(message.toString())
                .setPositiveButton("OK", null)
                .show();
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
        portraitLayout = getResources().getConfiguration().orientation
                == Configuration.ORIENTATION_PORTRAIT;

        FrameLayout root = new FrameLayout(this);
        root.setClipChildren(false);
        root.setClipToPadding(false);

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

        // Slightly mute the background artwork without changing the image asset.
        View backgroundDim = new View(this);
        backgroundDim.setBackgroundColor(Color.argb(26, 0, 0, 0)); // ~10% black
        backgroundDim.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        root.addView(backgroundDim, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        scroll.setClipChildren(false);
        scroll.setVerticalScrollBarEnabled(true);
        scroll.setScrollbarFadingEnabled(false);
        scroll.setSmoothScrollingEnabled(true);

        LinearLayout content = new LinearLayout(this);
        content.setClipChildren(false);
        content.setClipToPadding(false);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER_HORIZONTAL);
        int sidePad = dp(portraitLayout ? 16 : 24);
        int topPad = dp(portraitLayout ? 14 : 28);
        content.setPadding(sidePad, topPad, sidePad, dp(portraitLayout ? 18 : 28));

        TextView title = new TextView(this);
        SpannableString heading = new SpannableString("The Simpsons: Hit & Run\n Revamped!");
        int revampedStart = heading.toString().indexOf("Revamped!");
        heading.setSpan(
                new ForegroundColorSpan(PINK),
                revampedStart,
                heading.length(),
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        title.setText(heading);
        title.setTextSize(portraitLayout ? 25 : 29);
        title.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);
        title.setTextColor(YELLOW);
        title.setGravity(Gravity.CENTER);
        title.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
        content.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView setup = new TextView(this);
        setup.setText("First-Time Setup");
        setup.setTextSize(portraitLayout ? 20 : 24);
        setup.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);
        setup.setTextColor(Color.WHITE);
        setup.setGravity(Gravity.CENTER);
        setup.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
        LinearLayout.LayoutParams setupParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        setupParams.setMargins(0, dp(portraitLayout ? 10 : 8), 0, 0);
        content.addView(setup, setupParams);

        if (portraitLayout) {
            TextView locationPrompt = new TextView(this);
            locationPrompt.setText("Choose where your game data is stored.");
            locationPrompt.setTextSize(17);
            locationPrompt.setTextColor(Color.WHITE);
            locationPrompt.setGravity(Gravity.CENTER);
            locationPrompt.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
            LinearLayout.LayoutParams promptParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            promptParams.setMargins(0, dp(24), 0, 0);
            content.addView(locationPrompt, promptParams);

            TextView copyInstruction = new TextView(this);
            copyInstruction.setText("Copy the complete game-data folder from the official PC release "
                    + "to the location you choose:");
            copyInstruction.setTextSize(17);
            copyInstruction.setTextColor(Color.WHITE);
            copyInstruction.setGravity(Gravity.CENTER);
            copyInstruction.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
            LinearLayout.LayoutParams copyParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            // The old blank line was about one text line high; use an explicit
            // gap roughly three times that size for easier visual separation.
            copyParams.setMargins(0, dp(54), 0, 0);
            content.addView(copyInstruction, copyParams);
        } else {
            // Preserve the existing landscape/TV instruction layout unchanged.
            TextView help = new TextView(this);
            help.setText("Choose where your game data is stored.\n\n"
                    + "Copy the complete game-data folder from the official PC release "
                    + "to the location you choose:");
            help.setTextSize(20);
            help.setTextColor(Color.WHITE);
            help.setGravity(Gravity.CENTER);
            help.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
            LinearLayout.LayoutParams helpParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            helpParams.setMargins(0, dp(12), 0, 0);
            content.addView(help, helpParams);
        }

        String saved = SimpsonsActivity.getSavedGameDataPath(this);
        if (saved != null && new File(saved).isDirectory()) {
            TextView current = new TextView(this);
            current.setText("\nCurrent: " + saved);
            current.setTextSize(portraitLayout ? 14 : 17);
            current.setTextColor(YELLOW);
            current.setGravity(Gravity.CENTER);
            current.setShadowLayer(dp(3), 0, dp(2), Color.BLACK);
            content.addView(current, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT));
        }

        if (portraitLayout) {
            // Draw the arrow as a stroked shape instead of a font glyph so its
            // thickness and proportions stay clear across Android devices.
            View downArrow = new View(this) {
                private final Paint arrowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
                private final Path arrowHead = new Path();

                {
                    arrowPaint.setColor(PINK);
                    arrowPaint.setStyle(Paint.Style.STROKE);
                    arrowPaint.setStrokeWidth(dp(8));
                    arrowPaint.setStrokeCap(Paint.Cap.ROUND);
                    arrowPaint.setStrokeJoin(Paint.Join.ROUND);
                    arrowPaint.setShadowLayer(dp(2), 0, dp(1), Color.BLACK);
                    setLayerType(View.LAYER_TYPE_SOFTWARE, null);
                    setContentDescription("Storage choices below");
                }

                @Override
                protected void onDraw(Canvas canvas) {
                    super.onDraw(canvas);
                    float centerX = getWidth() / 2.0f;
                    float bottom = getHeight() - dp(5);
                    float headBaseY = bottom - dp(24);
                    float headHalfWidth = dp(23);

                    canvas.drawLine(centerX, dp(3), centerX, headBaseY, arrowPaint);
                    arrowHead.reset();
                    arrowHead.moveTo(centerX - headHalfWidth, headBaseY);
                    arrowHead.lineTo(centerX, bottom);
                    arrowHead.lineTo(centerX + headHalfWidth, headBaseY);
                    canvas.drawPath(arrowHead, arrowPaint);
                }
            };
            downArrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
            LinearLayout.LayoutParams arrowParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    dp(66));
            arrowParams.setMargins(0, dp(8), 0, dp(12));
            content.addView(downArrow, arrowParams);
        }

        Button sharedInternal = new Button(this);
        styleButton(sharedInternal);
        sharedInternal.setText("Use Shared Internal Storage\nInternal storage/Simpsons");
        sharedInternal.setOnClickListener(v -> useSharedInternalStorage());
        content.addView(sharedInternal, buttonParams());

        Button internal = new Button(this);
        styleButton(internal);
        internal.setText("Use Internal Storage\n/Android/data/com.sevarg.simpsons/files");
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
        scroll.post(() -> {
            scroll.scrollTo(0, 0);
            sharedInternal.requestFocus();
        });
    }

    private void styleButton(Button button) {
        button.setTextColor(DARK_BLUE);
        button.setAllCaps(false);
        button.setTextSize(televisionLayout ? 19 : 16);
        button.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);
        button.setGravity(Gravity.CENTER);
        button.setFocusable(true);
        button.setFocusableInTouchMode(true);
        button.setMinHeight(dp(portraitLayout ? 72 : 82));
        button.setPadding(
                dp(portraitLayout ? 14 : 20),
                dp(portraitLayout ? 10 : 14),
                dp(portraitLayout ? 14 : 20),
                dp(portraitLayout ? 10 : 14));

        GradientDrawable normal = new GradientDrawable();
        normal.setColor(YELLOW);
        normal.setCornerRadius(dp(14));

        GradientDrawable focused = new GradientDrawable();
        focused.setColor(YELLOW);
        focused.setCornerRadius(dp(14));
        focused.setStroke(dp(5), PINK);

        StateListDrawable states = new StateListDrawable();
        states.addState(new int[] { android.R.attr.state_focused }, focused);
        states.addState(new int[] { android.R.attr.state_pressed }, focused);
        states.addState(new int[] {}, normal);

        button.setBackground(states);

        final ValueAnimator[] pulseAnimator = new ValueAnimator[1];
        button.setOnFocusChangeListener((v, hasFocus) -> {
            button.setTextColor(hasFocus ? Color.WHITE : DARK_BLUE);

            if (pulseAnimator[0] != null) {
                pulseAnimator[0].cancel();
                pulseAnimator[0] = null;
            }
            button.setScaleX(1.0f);
            button.setScaleY(1.0f);

            if (!hasFocus) {
                return;
            }

            button.post(() -> {
                if (!button.hasFocus()) {
                    return;
                }

                final float safeScaleX = getSafeFocusedScaleX(button);
                ValueAnimator animator = ValueAnimator.ofFloat(0.0f, 1.0f);
                animator.setDuration(700);
                animator.setRepeatMode(ValueAnimator.REVERSE);
                animator.setRepeatCount(ValueAnimator.INFINITE);
                animator.setInterpolator(new AccelerateDecelerateInterpolator());
                animator.addUpdateListener(animation -> {
                    float fraction = (Float) animation.getAnimatedValue();
                    button.setScaleX(1.0f + (safeScaleX - 1.0f) * fraction);
                    button.setScaleY(1.0f + 0.04f * fraction);
                });
                pulseAnimator[0] = animator;
                animator.start();
            });
        });
    }

    private float getSafeFocusedScaleX(Button button) {
        View rootView = button.getRootView();
        int buttonWidth = button.getWidth();
        if (rootView == null || buttonWidth <= 0 || rootView.getWidth() <= 0) {
            return 1.0f;
        }

        int[] buttonLocation = new int[2];
        int[] rootLocation = new int[2];
        button.getLocationOnScreen(buttonLocation);
        rootView.getLocationOnScreen(rootLocation);

        float centerX = buttonLocation[0] - rootLocation[0] + buttonWidth / 2.0f;
        float viewWidth = rootView.getWidth();
        float safeMargin = dp(8);
        float availableHalfWidth = Math.min(centerX, viewWidth - centerX) - safeMargin;
        if (availableHalfWidth <= 0) {
            return 1.0f;
        }

        float maxSafeScale = (2.0f * availableHalfWidth) / buttonWidth;
        // Keep horizontal growth very modest; vertical growth remains the
        // more visible pulse, and fixed side margins provide further clearance.
        return Math.max(1.0f, Math.min(1.025f, maxSafeScale));
    }

    private LinearLayout.LayoutParams buttonParams() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        // The side margins reserve real layout space for the pulsing transform.
        p.setMargins(dp(24), dp(portraitLayout ? 10 : 18), dp(24), 0);
        return p;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
