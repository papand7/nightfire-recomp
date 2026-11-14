package io.github.papand7.nightfire;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONObject;

/** Responsive native launcher. The APK contains no game data; PackageStore imports the player's package. */
public class LauncherActivity extends Activity {
    private static final int PICK_ZIP = 1, PICK_FOLDER = 2;
    private static final int NAVY = 0xFF080F1C, PANEL = 0xFF101F34, BORDER = 0xFF253B56,
            ACCENT = 0xFF438DFF, CYAN = 0xFF87CFFF, TEXT = 0xFFF0F5FF, DIM = 0xFFABBDD3;
    private TextView status, packageTitle, badge, importStatus, header, tagline;
    private ProgressBar progress;
    private Button play, importZip, importFolder, settings, switchGame;
    private boolean resumed;
    private final Runnable importObserver = this::renderImport;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout screen = new FrameLayout(this);
        screen.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[] { NAVY, 0xFF10233E }));
        // Target SDK 35 draws behind the system bars. Content stays inside bars and camera cutouts.
        screen.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                v.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            } else {
                v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets.consumeSystemWindowInsets();
        });
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        screen.addView(scroll, new FrameLayout.LayoutParams(-1, -1));
        FrameLayout centre = new FrameLayout(this);
        scroll.addView(centre, new ScrollView.LayoutParams(-1, -2));

        LinearLayout columns = column();
        LinearLayout mission = card();
        add(mission, text("MISSION CONTROL", 11, CYAN, true), 0);
        add(mission, text("Your next mission awaits.", 30, TEXT, true), 12);
        tagline = text("", 15, DIM, false);
        add(mission, tagline, 10);
        badge = text("", 11, CYAN, true);
        badge.setPadding(dp(12), dp(7), dp(12), dp(7));
        badge.setBackground(shape(0xFF192E49, BORDER, 8));
        LinearLayout.LayoutParams tag = new LinearLayout.LayoutParams(-2, -2);
        tag.topMargin = dp(28);
        mission.addView(badge, tag);
        packageTitle = text("", 21, TEXT, true);
        add(mission, packageTitle, 16);
        status = text("", 14, DIM, false);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        add(mission, status, 8);
        play = button("Play", true);
        play.setOnClickListener(v -> {
            if (ImportTask.active) return;
            if (PackageStore.problem(this) == null)
                startActivity(new Intent(this, GameActivity.class).putExtra(GameActivity.EXTRA_GAME, PackageStore.current(this)));
            else pick(PICK_ZIP);
        });
        add(mission, play, 24);
        add(mission, text("Touch controls and gamepad supported.", 12, DIM, false), 12);
        switchGame = button("Switch game", false);
        switchGame.setOnClickListener(v -> switchGame());
        add(mission, switchGame, 18);

        LinearLayout setup = card();
        add(setup, text("Your game", 21, TEXT, true), 0);
        add(setup, text("Bring your own copy. Set up once, then play whenever you like.", 14, DIM, false), 8);
        step(setup, "1", "Build your package", "In the desktop launcher's Android page, create your game package.");
        step(setup, "2", "Copy it here", "Transfer the ZIP to this device using USB or a file share.");
        step(setup, "3", "Import and play", "Choose your package below. Your saves stay when you update it.");
        importZip = button("Import game package", true);
        importZip.setOnClickListener(v -> pick(PICK_ZIP));
        add(setup, importZip, 24);
        importFolder = button("Import an unpacked folder", false);
        importFolder.setOnClickListener(v -> pick(PICK_FOLDER));
        add(setup, importFolder, 10);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setProgressTintList(ColorStateList.valueOf(ACCENT));
        progress.setIndeterminateTintList(ColorStateList.valueOf(ACCENT));
        progress.setMax(1000);
        add(setup, progress, 18);
        importStatus = text("", 14, DIM, false);
        add(setup, importStatus, 8);
        settings = button("Game settings", false);
        settings.setOnClickListener(v -> settings());
        add(setup, settings, 18);

        ResponsivePage page = new ResponsivePage(columns, mission, setup);
        header = text("007", 24, TEXT, true);
        add(page, header, 0);
        TextView edition = text("ANDROID EDITION", 11, CYAN, true);
        edition.setLetterSpacing(0.16f);
        add(page, edition, 6);
        add(page, columns, 28);
        add(page, text("Built from your own disc. Your game stays on your device.", 12, DIM, false), 24);
        centre.addView(page, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        setContentView(screen);
        screen.requestApplyInsets();
        ImportTask.observer = importObserver;
        renderImport();
    }

    /** Available width, not device type or orientation, decides the layout. Text can grow and the page can scroll. */
    private class ResponsivePage extends LinearLayout {
        final LinearLayout columns, mission, setup;
        Boolean wide;
        ResponsivePage(LinearLayout columns, LinearLayout mission, LinearLayout setup) {
            super(LauncherActivity.this);
            this.columns = columns; this.mission = mission; this.setup = setup;
            setOrientation(VERTICAL);
        }
        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            int width = Math.min(MeasureSpec.getSize(widthSpec), dp(1120));
            int edge = width >= dp(600) ? dp(32) : dp(16);
            setPadding(edge, dp(28), edge, dp(28));
            float fontScale = getResources().getConfiguration().fontScale;
            boolean next = width - 2 * edge >= dp(720 * Math.max(1, fontScale));
            if (wide == null || wide != next) {
                wide = next;
                columns.removeAllViews();
                columns.setOrientation(next ? HORIZONTAL : VERTICAL);
                LinearLayout.LayoutParams left = new LinearLayout.LayoutParams(next ? 0 : -1, -2, next ? 1 : 0);
                LinearLayout.LayoutParams right = new LinearLayout.LayoutParams(next ? 0 : -1, -2, next ? 1 : 0);
                if (next) right.leftMargin = dp(20); else right.topMargin = dp(16);
                columns.addView(mission, left);
                columns.addView(setup, right);
                columns.setBaselineAligned(false);
            }
            super.onMeasure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), heightSpec);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        ImportTask.observer = importObserver;
        renderImport();
    }
    @Override protected void onPause() { resumed = false; super.onPause(); }
    @Override protected void onDestroy() {
        if (ImportTask.observer == importObserver) ImportTask.observer = null;
        super.onDestroy();
    }

    private void refresh() {
        if (ImportTask.active) return;
        String why = PackageStore.problem(this);
        JSONObject m = PackageStore.manifest(this);
        boolean ready = why == null && m != null;
        badge.setText(ready ? "READY TO PLAY" : m == null ? "SETUP REQUIRED" : "PACKAGE NEEDS ATTENTION");
        badge.setTextColor(ready ? 0xFFA0E5CB : CYAN);
        String title = m != null ? m.optString("title", "007: NightFire") : null;
        header.setText(title != null ? title.toUpperCase(java.util.Locale.ROOT).replace(": ", " / ") : "007");
        tagline.setText(title != null ? title + ", ready for a new screen." : "Your 007 games, ready for a new screen.");
        packageTitle.setText(ready ? title : m == null ? "Add your game" : "Check your package");
        String date = m != null ? m.optString("built", "") : "";
        if (date.length() >= 10) date = date.substring(0, 10);
        status.setText(ready ? "Installed on this device" + (date.isEmpty() ? "." : " \u00b7 Built " + date)
                : m == null ? "Import the package you created on your PC to get started." : why);
        play.setText(ready ? "Play " + title.replaceFirst("^007: *", "") : "Import game package");
        switchGame.setVisibility(PackageStore.games(this).size() > 1 ? View.VISIBLE : View.GONE);
        enable(switchGame, true);
        enable(play, true);
        enable(settings, ready);
        importZip.setText(ready ? "Import an updated package" : "Import game package");
    }

    private void renderImport() {
        boolean busy = ImportTask.active;
        enable(importZip, !busy);
        enable(importFolder, !busy);
        progress.setVisibility(busy ? View.VISIBLE : View.GONE);
        importStatus.setVisibility(busy ? View.VISIBLE : View.GONE);
        if (busy) {
            enable(play, false); enable(settings, false); enable(switchGame, false);
            play.setText("Importing...");
            badge.setText("IMPORT IN PROGRESS");
            packageTitle.setText("Getting your game ready");
            status.setText("Keep the app open while your package is imported.");
            progress.setIndeterminate(ImportTask.total <= 0);
            if (ImportTask.total > 0)
                progress.setProgress((int) Math.min(1000, 1000.0 * ImportTask.done / ImportTask.total));
            importStatus.setText(ImportTask.done == 0 ? "Preparing the package..." : String.format(java.util.Locale.getDefault(),
                    ImportTask.total > 0 ? "Importing \u00b7 %.0f MB of %.0f MB" : "Importing \u00b7 %.0f MB copied",
                    ImportTask.done / 1e6, ImportTask.total / 1e6));
        } else {
            refresh();
            if (resumed && ImportTask.finished) {
                ImportTask.finished = false;
                if (ImportTask.error != null) message("Couldn't import the package", ImportTask.error);
                else message("Ready to play", "Your game is installed. You can now remove the transferred ZIP to free up space.");
            }
        }
    }

    private void pick(int what) {
        if (ImportTask.active) return;
        Intent i;
        if (what == PICK_ZIP) {
            i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "application/zip", "application/x-zip-compressed", "application/octet-stream" });
        } else i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        startActivityForResult(i, what);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if ((request != PICK_ZIP && request != PICK_FOLDER) || result != RESULT_OK || data == null || data.getData() == null) return;
        if (!ImportTask.active) ImportTask.start(getApplicationContext(), data.getData(), request == PICK_ZIP);
    }

    /** Application-owned work survives activity recreation without retaining a destroyed Activity or its views. */
    private static final class ImportTask {
        static final Handler UI = new Handler(Looper.getMainLooper());
        static boolean active, finished;
        static long done, total;
        static String error;
        static Runnable observer;
        static void notifyUi() { if (observer != null) observer.run(); }
        static void start(Context app, Uri uri, boolean zip) {
            active = true; finished = false; error = null; done = total = 0;
            notifyUi();
            new Thread(() -> {
                String failure = null;
                try {
                    long[] last = { 0 };
                    PackageStore.Progress callback = (copied, size, name) -> {
                        long now = SystemClock.uptimeMillis();
                        if (now - last[0] < 100) return;
                        last[0] = now;
                        UI.post(() -> {
                            done = copied;
                            // ZIP's byte size is compressed; copied counts unpacked bytes, so no false percentage.
                            total = zip ? 0 : size;
                            notifyUi();
                        });
                    };
                    if (zip) PackageStore.importZip(app, uri, callback);
                    else PackageStore.importTree(app, uri, callback);
                } catch (Exception e) { failure = e.getMessage() != null ? e.getMessage() : e.toString(); }
                final String result = failure;
                UI.post(() -> { active = false; finished = true; error = result; notifyUi(); });
            }, "game-import").start();
        }
    }

    /** the installed games: one picked as the current one, or the current one removed */
    private void switchGame() {
        if (ImportTask.active) return;
        java.util.List<String> ids = PackageStore.games(this);
        if (ids.isEmpty()) return;
        String[] titles = new String[ids.size()];
        String current = PackageStore.current(this);
        int checked = 0;
        for (int i = 0; i < ids.size(); i++) {
            JSONObject m = PackageStore.manifest(this, ids.get(i));
            titles[i] = m != null ? m.optString("title", ids.get(i)) : ids.get(i);
            if (ids.get(i).equals(current)) checked = i;
        }
        new AlertDialog.Builder(this).setTitle("Your games")
            .setSingleChoiceItems(titles, checked, (d, w) -> {
                PackageStore.setCurrent(this, ids.get(w));
                d.dismiss();
                refresh();
            })
            .setNeutralButton("Remove current game", (d, w) -> removeGame())
            .setNegativeButton("Close", null).show();
    }

    private void removeGame() {
        String id = PackageStore.current(this);
        JSONObject m = PackageStore.manifest(this);
        if (id == null || ImportTask.active) return;
        String title = m != null ? m.optString("title", id) : id;
        new AlertDialog.Builder(this).setTitle("Remove " + title + "?")
            .setMessage("Its package, saves and settings are deleted from this device. Import its package again to play it.")
            .setPositiveButton("Remove", (d, w) -> {
                PackageStore.remove(this, id);
                refresh();
            }).setNegativeButton("Cancel", null).show();
    }

    private void settings() {
        if (ImportTask.active || PackageStore.problem(this) != null) return;
        LinearLayout box = column();
        box.setPadding(dp(24), dp(8), dp(24), dp(16));
        final String[] scales = { "50", "67", "75", "100" }, caps = { "30", "60", "0" };
        Spinner scale = spinner(new String[] { "50% \u00b7 Fastest", "67% \u00b7 Balanced", "75% \u00b7 Detailed", "100% \u00b7 Sharpest" });
        scale.setSelection(indexOf(scales, PackageStore.readSetting(this, "render_percent"), 1));
        Spinner cap = spinner(new String[] { "30 FPS \u00b7 Battery saver", "60 FPS \u00b7 Smooth", "Unlimited" });
        cap.setSelection(indexOf(caps, PackageStore.readSetting(this, "fps_cap"), 1));
        CheckBox fps = check("Show frame rate", "true".equals(PackageStore.readSetting(this, "show_fps")));
        CheckBox intro = check("Skip intro movies", !"false".equals(PackageStore.readSetting(this, "skip_intro")));
        add(box, text("Render resolution", 14, DIM, false), 12);
        add(box, scale, 4);
        add(box, text("Frame rate", 14, DIM, false), 16);
        add(box, cap, 4);
        add(box, fps, 16);
        add(box, intro, 4);
        add(box, text("More options are available in the game's Settings menu.", 13, DIM, false), 16);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(box);
        new AlertDialog.Builder(this).setTitle("Game settings").setView(scroll)
            .setPositiveButton("Save", (d, w) -> {
                try {
                    PackageStore.writeSetting(this, "render_percent", scales[scale.getSelectedItemPosition()]);
                    PackageStore.writeSetting(this, "fps_cap", caps[cap.getSelectedItemPosition()]);
                    PackageStore.writeSetting(this, "show_fps", fps.isChecked() ? "true" : "false");
                    PackageStore.writeSetting(this, "skip_intro", intro.isChecked() ? "true" : "false");
                } catch (Exception e) { message("Settings weren't saved", e.getMessage()); }
            }).setNegativeButton("Cancel", null).show();
    }

    private int dp(float v) { return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics())); }
    private LinearLayout column() {
        LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v;
    }
    private LinearLayout card() {
        LinearLayout v = column();
        v.setPadding(dp(24), dp(24), dp(24), dp(24));
        v.setBackground(shape(PANEL, BORDER, 20));
        return v;
    }
    private TextView text(String value, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value); t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp); t.setTextColor(color);
        t.setTypeface(Typeface.create("sans-serif", bold ? Typeface.BOLD : Typeface.NORMAL));
        t.setIncludeFontPadding(false);
        t.setLineSpacing(dp(3), 1);
        return t;
    }
    private void add(LinearLayout parent, View child, int gap) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2); lp.topMargin = dp(gap);
        parent.addView(child, lp);
    }
    private void step(LinearLayout parent, String number, String title, String body) {
        LinearLayout row = new LinearLayout(this);
        TextView n = text(number, 13, CYAN, true);
        n.setGravity(Gravity.CENTER); n.setBackground(shape(0xFF192E49, BORDER, 10));
        row.addView(n, new LinearLayout.LayoutParams(dp(32), dp(32)));
        LinearLayout words = column();
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, -2, 1); lp.leftMargin = dp(12);
        row.addView(words, lp);
        add(words, text(title, 15, TEXT, true), 0);
        add(words, text(body, 13, DIM, false), 5);
        add(parent, row, 20);
    }
    private GradientDrawable shape(int fill, int stroke, int radius) {
        GradientDrawable d = new GradientDrawable(); d.setColor(fill); d.setCornerRadius(dp(radius));
        d.setStroke(dp(1), stroke); return d;
    }
    private Button button(String label, boolean primary) {
        Button b = new Button(this);
        b.setText(label); b.setAllCaps(false); b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        b.setSingleLine(false); b.setMaxLines(3); b.setEllipsize(null);
        b.setTextColor(primary ? Color.WHITE : TEXT);
        b.setMinHeight(dp(56)); b.setMinimumWidth(0); b.setMinWidth(0);
        b.setPadding(dp(16), dp(14), dp(16), dp(14));
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[] { android.R.attr.state_focused }, shape(primary ? ACCENT : 0xFF203B5C, CYAN, 12));
        states.addState(new int[] {}, shape(primary ? 0xFF286AD1 : 0xFF14273F, primary ? ACCENT : BORDER, 12));
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x4487CFFF), states, shape(Color.WHITE, Color.WHITE, 12)));
        return b;
    }
    private void enable(Button button, boolean enabled) { button.setEnabled(enabled); button.setAlpha(enabled ? 1 : 0.4f); }
    private CheckBox check(String label, boolean checked) {
        CheckBox c = new CheckBox(this); c.setText(label); c.setTextColor(TEXT); c.setTextSize(15);
        c.setButtonTintList(ColorStateList.valueOf(CYAN)); c.setMinHeight(dp(52)); c.setChecked(checked); return c;
    }
    private Spinner spinner(String[] items) {
        Spinner spinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, items) {
            @Override public View getView(int position, View convertView, ViewGroup parent) {
                TextView t = (TextView) super.getView(position, convertView, parent);
                t.setSingleLine(false); t.setMaxLines(3); t.setTextSize(16); t.setTextColor(TEXT);
                t.setPadding(dp(12), dp(12), dp(12), dp(12)); return t;
            }
            @Override public View getDropDownView(int position, View convertView, ViewGroup parent) {
                TextView t = (TextView) super.getDropDownView(position, convertView, parent);
                t.setSingleLine(false); t.setMaxLines(3); t.setTextSize(16); t.setTextColor(TEXT);
                t.setPadding(dp(16), dp(14), dp(16), dp(14)); return t;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter); spinner.setMinimumHeight(dp(56)); return spinner;
    }
    private static int indexOf(String[] values, String value, int fallback) {
        for (int i = 0; value != null && i < values.length; i++) if (values[i].equals(value)) return i;
        return fallback;
    }
    private void message(String title, String body) {
        new AlertDialog.Builder(this).setTitle(title).setMessage(body).setPositiveButton("OK", null).show();
    }
}
