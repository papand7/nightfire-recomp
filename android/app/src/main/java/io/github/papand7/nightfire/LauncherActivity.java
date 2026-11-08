package io.github.papand7.nightfire;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONObject;

/**
 * The launcher: Play, Import package and a few settings, in the modern menus' blues. The game package is built on the
 * player's PC by the NightFire launcher from their own disc and copied to the phone; this imports it into the app's
 * private storage (PackageStore).
 */
public class LauncherActivity extends Activity {
    private static final int PICK_ZIP = 1, PICK_FOLDER = 2;
    private static final int NAVY = 0xFF061430, BLUE = 0xFF0C2C5E, ACCENT = 0xFF3C8CFF, TEXT = 0xFFE1EEFF,
                             DIM = 0xFF9DB8E0;

    private TextView status;
    private ProgressBar progress;
    private Button play, importZip, importFolder, settings;
    private boolean busy;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { NAVY, BLUE });
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(32), dp(24), dp(32), dp(24));

        TextView title = text("007: NightFire", 34, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title);
        root.addView(text("Android (beta): your own game, built from your disc on your PC", 14, DIM));

        status = text("", 15, TEXT);
        status.setPadding(0, dp(18), 0, dp(12));
        root.addView(status);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        root.addView(progress, new LinearLayout.LayoutParams(dp(420), LinearLayout.LayoutParams.WRAP_CONTENT));

        play = button("Play", true);
        play.setOnClickListener(v -> play());
        importZip = button("Import package (.zip)", false);
        importZip.setOnClickListener(v -> pick(PICK_ZIP));
        importFolder = button("Import package folder", false);
        importFolder.setOnClickListener(v -> pick(PICK_FOLDER));
        settings = button("Settings", false);
        settings.setOnClickListener(v -> settings());
        for (Button b : new Button[] { play, importZip, importFolder, settings }) {
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(320), dp(52));
            lp.topMargin = dp(10);
            root.addView(b, lp);
        }
        TextView help = text("On your PC: the NightFire launcher's Android package (or sdk/scripts/android/package.sh) makes "
                             + "NightFire-android-package.zip from your disc. Copy it to this phone (USB or a file share), "
                             + "then Import package. A controller works too; the touch controls hide while one is connected.",
                             12, DIM);
        help.setPadding(0, dp(18), 0, 0);
        root.addView(help, new LinearLayout.LayoutParams(dp(520), LinearLayout.LayoutParams.WRAP_CONTENT));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackground(bg);
        scroll.setFillViewport(true);
        scroll.addView(root);
        setContentView(scroll);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        if (busy) return;
        String why = PackageStore.problem(this);
        JSONObject m = PackageStore.manifest(this);
        if (why == null && m != null) {
            status.setText("Game package: " + m.optString("title", "the game") + ", built " + m.optString("built", "?")
                           + " (" + m.optString("abi", "") + ")");
        } else {
            status.setText(why);
        }
        play.setEnabled(why == null);
        play.setAlpha(why == null ? 1f : 0.45f);
    }

    private void play() {
        startActivity(new Intent(this, GameActivity.class));
    }

    private void pick(int what) {
        if (busy) return;
        Intent i;
        if (what == PICK_ZIP) {
            i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "application/zip", "application/x-zip-compressed",
                                                                "application/octet-stream" });
        } else {
            i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        }
        startActivityForResult(i, what);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        final boolean zip = request == PICK_ZIP;
        busy = true;
        setButtons(false);
        progress.setVisibility(View.VISIBLE);
        progress.setProgress(0);
        status.setText("Importing the game package...");
        new Thread(() -> {
            String error = null;
            try {
                PackageStore.Progress p = (done, total, what) -> ui.post(() -> {
                    if (total > 0) progress.setProgress((int) (done * 1000 / total));
                    status.setText(String.format("Importing: %.0f MB%s", done / 1e6,
                                                 total > 0 ? String.format(" of %.0f MB", total / 1e6) : ""));
                });
                if (zip) PackageStore.importZip(this, uri, p);
                else PackageStore.importTree(this, uri, p);
            } catch (Exception e) {
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String err = error;
            ui.post(() -> {
                busy = false;
                progress.setVisibility(View.GONE);
                setButtons(true);
                refresh();
                if (err != null) message("Import failed", err);
                else message("Imported", "The game package is ready: Play. You may delete the .zip from the phone now.");
            });
        }, "import").start();
    }

    private void settings() {
        if (PackageStore.manifest(this) == null) {
            message("Settings", "Import a game package first.");
            return;
        }
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(20), dp(12), dp(20), 0);
        final String[] scales = { "50", "67", "75", "100" };
        final Spinner scale = spinner(new String[] { "50% (fastest)", "67%", "75%", "100% (sharpest)" });
        scale.setSelection(indexOf(scales, PackageStore.readSetting(this, "render_percent"), 1));
        final String[] caps = { "30", "60", "0" };
        final Spinner cap = spinner(new String[] { "30 frames a second (cooler, longer battery)", "60 frames a second",
                                                    "Unlimited" });
        cap.setSelection(indexOf(caps, PackageStore.readSetting(this, "fps_cap"), 1));
        final CheckBox fps = new CheckBox(this);
        fps.setText("Show the frame rate");
        fps.setChecked("true".equals(PackageStore.readSetting(this, "show_fps")));
        final CheckBox intro = new CheckBox(this);
        intro.setText("Skip the intro movies");
        intro.setChecked(!"false".equals(PackageStore.readSetting(this, "skip_intro")));
        box.addView(label("Render resolution"));
        box.addView(scale);
        box.addView(label("Frame rate"));
        box.addView(cap);
        box.addView(fps);
        box.addView(intro);
        box.addView(label("The game's own menus (Options, PC Settings) have the rest."));
        new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
            .setTitle("Settings")
            .setView(box)
            .setPositiveButton("Save", (d, w) -> {
                try {
                    PackageStore.writeSetting(this, "render_percent", scales[scale.getSelectedItemPosition()]);
                    PackageStore.writeSetting(this, "fps_cap", caps[cap.getSelectedItemPosition()]);
                    PackageStore.writeSetting(this, "show_fps", fps.isChecked() ? "true" : "false");
                    PackageStore.writeSetting(this, "skip_intro", intro.isChecked() ? "true" : "false");
                } catch (Exception e) {
                    message("Settings", "Not saved: " + e.getMessage());
                }
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    // ---- views

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(String s, float sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER_HORIZONTAL);
        return t;
    }

    private TextView label(String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setPadding(0, dp(10), 0, dp(2));
        return t;
    }

    private Button button(String s, boolean main) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, main ? 20 : 16);
        b.setTextColor(main ? Color.WHITE : TEXT);
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(10));
        d.setColor(main ? ACCENT : 0x332A5CA8);
        d.setStroke(dp(2), main ? 0xFF8CC0FF : 0xFF4A78C0);
        b.setBackground(d);
        return b;
    }

    private Spinner spinner(String[] items) {
        Spinner s = new Spinner(this);
        ArrayAdapter<String> a = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, items);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        s.setAdapter(a);
        return s;
    }

    private static int indexOf(String[] values, String v, int fallback) {
        if (v != null)
            for (int i = 0; i < values.length; i++)
                if (values[i].equals(v)) return i;
        return fallback;
    }

    private void setButtons(boolean on) {
        for (Button b : new Button[] { importZip, importFolder, settings }) b.setEnabled(on);
        play.setEnabled(on && PackageStore.problem(this) == null);
    }

    private void message(String title, String body) {
        new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
            .setTitle(title)
            .setMessage(body)
            .setPositiveButton("OK", null)
            .show();
    }
}
