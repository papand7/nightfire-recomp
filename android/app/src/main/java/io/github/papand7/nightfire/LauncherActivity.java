package io.github.papand7.nightfire;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Insets;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.List;
import java.util.Locale;

/**
 * Responsive native launcher, styled after the 007 Remastered PC launcher: navy night, scan lines and gun-barrel rings,
 * game tabs, numbered corner-bracket cards, a slanted PLAY button. The APK contains no game data;
 * PackageStore imports the player's package.
 */
public class LauncherActivity extends Activity {
    private static final int PICK_ZIP = 1, PICK_FOLDER = 2;
    // the PC launcher's palette (style.css)
    private static final int BG = 0xFF050E24, PANEL = 0xD109183C, LINE = 0x382878FF,
            LINE2 = 0x12FFFFFF, TEXT = 0xFFE6F0FF, MUTED = 0xFF8AA2C8, ICE = 0xFFE0F2FF, BLUE = 0xFF4A90FF,
            DEEP = 0xFF1D4FC4, GOLD = 0xFF78D2FF, OK = 0xFF7ED492, BAD = 0xFFFF6B55;
    private static final String COND = "sans-serif-condensed";

    private TextView status, packageTitle, packageMeta, importStatus, heroName, heroPrefix, settingsNote, saveMsg;
    private View dot;
    private ProgressBar progress;
    private Button play, importZip, importFolder, toSettings, save, remove;
    private LinearLayout gameTabs, home, settingsPage, navRow;
    private TextView navMission, navSettings;
    private Seg scaleSeg, capSeg;
    private Switch fpsSwitch, introSwitch;
    private boolean resumed, onSettings;
    private final Runnable importObserver = this::renderImport;
    private static final String[] SCALES = { "50", "67", "75", "100" }, CAPS = { "30", "60", "0" };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout screen = new FrameLayout(this);
        screen.setBackground(new Night());
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
        LinearLayout frame = column();
        screen.addView(frame, new FrameLayout.LayoutParams(-1, -1));

        // ---- top bar: brand + game tabs
        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0xFF0A1A40, 0xFF06122E }));
        TextView brand = text("007 REMASTERED", 11, ICE, true, true);
        brand.setLetterSpacing(0.18f);
        brand.setPadding(dp(12), 0, dp(6), 0);
        bar.addView(brand, new LinearLayout.LayoutParams(-2, -2));
        HorizontalScrollView tabScroll = new HorizontalScrollView(this);
        tabScroll.setHorizontalScrollBarEnabled(false);
        gameTabs = new LinearLayout(this);
        tabScroll.addView(gameTabs, new FrameLayout.LayoutParams(-2, -1));
        bar.addView(tabScroll, new LinearLayout.LayoutParams(0, -1, 1));
        frame.addView(bar, new LinearLayout.LayoutParams(-1, dp(44)));
        frame.addView(rule(LINE), new LinearLayout.LayoutParams(-1, dp(1)));

        // ---- page navigation (the PC launcher's sidebar, as a strip)
        navRow = new LinearLayout(this);
        navRow.setBackgroundColor(0xF2061028);
        navMission = navItem("MISSION", false);
        navSettings = navItem("SETTINGS", true);
        navRow.addView(navMission, new LinearLayout.LayoutParams(-2, dp(42)));
        navRow.addView(navSettings, new LinearLayout.LayoutParams(-2, dp(42)));
        LinearLayout.LayoutParams navLp = new LinearLayout.LayoutParams(-1, -2);
        frame.addView(navRow, navLp);
        frame.addView(rule(LINE2), new LinearLayout.LayoutParams(-1, dp(1)));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        frame.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        FrameLayout centre = new FrameLayout(this);
        scroll.addView(centre, new ScrollView.LayoutParams(-1, -2));
        ResponsivePage page = new ResponsivePage();
        centre.addView(page, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL));

        // ---- MISSION
        home = column();
        Hero hero = new Hero();
        home.addView(hero, new LinearLayout.LayoutParams(-1, -2));

        Card game = new Card("01", "Your game");
        LinearLayout line = new LinearLayout(this);
        line.setGravity(Gravity.CENTER_VERTICAL);
        dot = new View(this);
        line.addView(dot, new LinearLayout.LayoutParams(dp(9), dp(9)));
        status = text("", 14, TEXT, true, false);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(0, -2, 1);
        sl.leftMargin = dp(10);
        line.addView(status, sl);
        add(game.body, line, 0);
        packageTitle = text("", 20, TEXT, true, true);
        add(game.body, packageTitle, 14);
        packageMeta = text("", 12, MUTED, false, false);
        packageMeta.setTypeface(Typeface.MONOSPACE);
        add(game.body, packageMeta, 6);
        toSettings = ghost("Settings");
        toSettings.setOnClickListener(v -> show(true));
        add(game.body, toSettings, 16);

        Card imp = new Card("02", "Import");
        TextView lead = text("Build your game package in the desktop launcher's Android page, copy the ZIP to this device, "
                + "then import it here. Your saves stay when you update it.", 13, MUTED, false, false);
        add(imp.body, lead, 0);
        importZip = primary("Import game package");
        importZip.setOnClickListener(v -> pick(PICK_ZIP));
        add(imp.body, importZip, 16);
        importFolder = ghost("Import an unpacked folder");
        importFolder.setOnClickListener(v -> pick(PICK_FOLDER));
        add(imp.body, importFolder, 10);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setProgressTintList(ColorStateList.valueOf(BLUE));
        progress.setIndeterminateTintList(ColorStateList.valueOf(BLUE));
        progress.setMax(1000);
        add(imp.body, progress, 14);
        importStatus = text("", 13, MUTED, false, false);
        add(imp.body, importStatus, 6);

        home.addView(new TwoCol(game, imp), lp(-1, -2, 0, 18));

        play = new Button(this);
        styleSlant(play);
        play.setOnClickListener(v -> {
            if (ImportTask.active) return;
            if (PackageStore.problem(this) == null)
                startActivity(new Intent(this, GameActivity.class).putExtra(GameActivity.EXTRA_GAME, PackageStore.current(this)));
            else pick(PICK_ZIP);
        });
        home.addView(play, lp(-2, dp(68), 0, 26));
        TextView touch = text("Touch controls and gamepad supported.", 12, MUTED, false, false);
        add(home, touch, 12);

        // ---- SETTINGS
        settingsPage = column();
        TextView h2 = text("SETTINGS", 30, ICE, true, true);
        h2.setLetterSpacing(0.14f);
        gradient(h2);
        add(settingsPage, h2, 0);
        settingsNote = text("", 13, MUTED, false, false);
        add(settingsPage, settingsNote, 6);

        Card disp = new Card("01", "Display");
        scaleSeg = new Seg(new String[] { "50%", "67%", "75%", "100%" });
        capSeg = new Seg(new String[] { "30 FPS", "60 FPS", "UNLIMITED" });
        setting(disp.body, "Render resolution", "50% is fastest, 100% sharpest.", scaleSeg, 0);
        setting(disp.body, "Frame rate", "30 FPS saves battery.", capSeg, 18);
        Card gameCard = new Card("02", "Game");
        fpsSwitch = toggle();
        introSwitch = toggle();
        setting(gameCard.body, "Show frame rate", "A small counter in a corner of the screen.", fpsSwitch, 0);
        setting(gameCard.body, "Skip intro movies", "Go straight to the menu.", introSwitch, 18);
        add(gameCard.body, text("More options are available in the game's Settings menu.", 12, MUTED, false, false), 18);
        settingsPage.addView(new TwoCol(disp, gameCard), lp(-1, -2, 0, 18));
        Card pkg = new Card("03", "Package");
        add(pkg.body, text("Removing a game deletes its package, saves and settings from this device.", 13, MUTED, false, false), 0);
        remove = ghost("Remove this game");
        remove.setTextColor(BAD);
        remove.setOnClickListener(v -> removeGame());
        add(pkg.body, remove, 14);
        settingsPage.addView(pkg, lp(-1, -2, 0, 18));
        LinearLayout saveRow = new LinearLayout(this);
        saveRow.setGravity(Gravity.CENTER_VERTICAL);
        saveMsg = text("", 13, ICE, false, false);
        saveRow.addView(saveMsg, new LinearLayout.LayoutParams(0, -2, 1));
        save = primary("Save");
        save.setOnClickListener(v -> saveSettings());
        saveRow.addView(save, new LinearLayout.LayoutParams(dp(160), -2));
        add(settingsPage, saveRow, 20);

        page.addView(home, new LinearLayout.LayoutParams(-1, -2));
        page.addView(settingsPage, new LinearLayout.LayoutParams(-1, -2));
        TextView foot = text("// CLASSIFIED: MI6 EYES ONLY", 10, 0x8C3C78E6, true, true);
        foot.setLetterSpacing(0.2f);
        add(page, foot, 32);
        add(page, text("Built from your own disc. Your game stays on your device.", 11, MUTED, false, false), 6);

        setContentView(screen);
        screen.requestApplyInsets();
        ImportTask.observer = importObserver;
        show(false);
        renderImport();
    }

    // ---------------------------------------------------------------- pages and tabs

    private void show(boolean settings) {
        onSettings = settings;
        home.setVisibility(settings ? View.GONE : View.VISIBLE);
        settingsPage.setVisibility(settings ? View.VISIBLE : View.GONE);
        markNav(navMission, !settings);
        markNav(navSettings, settings);
        if (settings) loadSettings();
    }

    private TextView navItem(String label, boolean settings) {
        TextView t = text(label, 12, MUTED, true, false);
        t.setLetterSpacing(0.14f);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(18), 0, dp(18), 0);
        t.setClickable(true); t.setFocusable(true);
        t.setOnClickListener(v -> show(settings));
        return t;
    }
    private void markNav(TextView t, boolean on) {
        t.setTextColor(on ? ICE : MUTED);
        GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                on ? new int[] { 0x242878FF, 0x00000000 } : new int[] { 0, 0 });
        t.setBackground(on ? new Underline(d, BLUE) : null);
    }

    private void renderTabs() {
        gameTabs.removeAllViews();
        List<String> ids = PackageStore.games(this);
        String current = PackageStore.current(this);
        if (ids.isEmpty()) {
            gameTabs.addView(tab("NO GAME YET", false, null), new LinearLayout.LayoutParams(-2, -1));
            return;
        }
        for (String id : ids) {
            JSONObject m = PackageStore.manifest(this, id);
            String name = shortName(m != null ? m.optString("title", id) : id);
            gameTabs.addView(tab(name, id.equals(current), id), new LinearLayout.LayoutParams(-2, -1));
        }
    }
    private TextView tab(String label, boolean on, String id) {
        TextView t = text(label.toUpperCase(Locale.ROOT), 11, on ? ICE : MUTED, true, false);
        t.setLetterSpacing(0.14f);
        t.setGravity(Gravity.CENTER);
        t.setSingleLine(true);
        t.setPadding(dp(11), 0, dp(11), 0);
        if (on) t.setBackground(new Underline(new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                new int[] { 0x142878FF, 0x142878FF }), ICE));
        if (id != null) {
            t.setClickable(true); t.setFocusable(true);
            t.setOnClickListener(v -> {
                if (ImportTask.active) return;
                PackageStore.setCurrent(this, id);
                refresh();
                if (onSettings) loadSettings();
            });
        }
        return t;
    }
    private static String shortName(String title) { return title.replaceFirst("^007: *", ""); }

    // ---------------------------------------------------------------- layout helpers

    /** Available width, not device type or orientation, decides the layout. Text can grow and the page can scroll. */
    private class ResponsivePage extends LinearLayout {
        ResponsivePage() { super(LauncherActivity.this); setOrientation(VERTICAL); }
        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            int width = Math.min(MeasureSpec.getSize(widthSpec), dp(1120));
            int edge = width >= dp(600) ? dp(32) : dp(16);
            setPadding(edge, dp(22), edge, dp(24));
            super.onMeasure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), heightSpec);
        }
    }

    /** Two cards side by side when there's room, stacked otherwise. */
    private class TwoCol extends LinearLayout {
        final View a, b;
        Boolean wide;
        TwoCol(View a, View b) {
            super(LauncherActivity.this);
            this.a = a; this.b = b;
            setBaselineAligned(false);
        }
        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            float fontScale = getResources().getConfiguration().fontScale;
            boolean next = MeasureSpec.getSize(widthSpec) >= dp(680 * Math.max(1, fontScale));
            if (wide == null || wide != next) {
                wide = next;
                removeAllViews();
                setOrientation(next ? HORIZONTAL : VERTICAL);
                LinearLayout.LayoutParams l = new LinearLayout.LayoutParams(next ? 0 : -1, -2, next ? 1 : 0);
                LinearLayout.LayoutParams r = new LinearLayout.LayoutParams(next ? 0 : -1, -2, next ? 1 : 0);
                if (next) r.leftMargin = dp(18); else r.topMargin = dp(18);
                addView(a, l);
                addView(b, r);
            }
            super.onMeasure(widthSpec, heightSpec);
        }
    }

    /** The night: navy, a glow from below, faint scan lines. */
    private class Night extends Drawable {
        final Paint p = new Paint();
        @Override public void draw(Canvas c) {
            int w = getBounds().width(), h = getBounds().height();
            c.drawColor(BG);
            p.setShader(new android.graphics.RadialGradient(w * 0.7f, h * 1.1f, Math.max(w, h) * 0.75f,
                    0x481446BE, 0x00000000, Shader.TileMode.CLAMP));
            c.drawRect(0, 0, w, h, p);
            p.setShader(null);
            p.setColor(0x05FFFFFF);
            for (int y = 0; y < h; y += dp(3)) c.drawRect(0, y, w, y + 1, p);
        }
        @Override public void setAlpha(int a) { }
        @Override public void setColorFilter(ColorFilter f) { }
        @Override public int getOpacity() { return PixelFormat.OPAQUE; }
    }

    /** The header: gun-barrel rings behind the JAMES BOND kicker and the two-tone title. */
    private class Hero extends FrameLayout {
        final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        Hero() {
            super(LauncherActivity.this);
            setWillNotDraw(false);
            setPadding(dp(18), dp(26), dp(18), dp(26));
            LinearLayout col = column();
            TextView kicker = text("JAMES BOND", 12, GOLD, true, false);
            kicker.setLetterSpacing(0.55f);
            col.addView(kicker);
            LinearLayout row = new LinearLayout(LauncherActivity.this);
            row.setGravity(Gravity.BOTTOM);
            heroPrefix = text("007:", 40, 0xFFD8DBE0, true, true);
            heroPrefix.setLetterSpacing(0.04f);
            heroName = text("NIGHTFIRE", 40, ICE, true, true);
            heroName.setLetterSpacing(0.1f);
            gradient(heroName);
            row.addView(heroPrefix, new LinearLayout.LayoutParams(-2, -2));
            LinearLayout.LayoutParams nl = new LinearLayout.LayoutParams(0, -2, 1);
            nl.leftMargin = dp(10);
            row.addView(heroName, nl);
            LinearLayout.LayoutParams rl = new LinearLayout.LayoutParams(-1, -2);
            rl.topMargin = dp(6);
            col.addView(row, rl);
            addView(col, new FrameLayout.LayoutParams(-1, -2, Gravity.CENTER_VERTICAL));
        }
        @Override protected void onDraw(Canvas c) {
            int w = getWidth(), h = getHeight();
            Paint f = new Paint();
            f.setShader(new LinearGradient(0, 0, w, 0, 0xF2040C20, 0x66040C20, Shader.TileMode.CLAMP));
            c.drawRect(0, 0, w, h, f);
            c.save();
            c.clipRect(0, 0, w, h);
            float cx = w - dp(30), cy = h / 2f;
            ring.setStyle(Paint.Style.STROKE);
            ring.setStrokeWidth(dp(1));
            for (int i = 1; i <= 9; i++) {
                ring.setColor(Color.argb(Math.max(8, 44 - i * 4), 200, 205, 215));
                c.drawCircle(cx, cy, dp(13) * i + dp(14), ring);
            }
            ring.setStyle(Paint.Style.FILL);
            ring.setColor(0xFF050506);
            c.drawCircle(cx, cy, dp(30), ring);
            c.restore();
            ring.setStyle(Paint.Style.FILL);
            ring.setColor(LINE);
            c.drawRect(0, h - dp(1), w, h, ring);
            super.onDraw(c);
        }
    }

    /** A dossier card: numbered head, panel body, corner marks top left and bottom right. */
    private class Card extends LinearLayout {
        final LinearLayout body = column();
        final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        Card(String num, String title) {
            super(LauncherActivity.this);
            setOrientation(VERTICAL);
            setWillNotDraw(false);
            LinearLayout head = new LinearLayout(LauncherActivity.this);
            head.setPadding(dp(18), dp(12), dp(18), dp(12));
            TextView n = text(num, 13, BLUE, true, true);
            n.setLetterSpacing(0.1f);
            head.addView(n);
            TextView t = text(title.toUpperCase(Locale.ROOT), 13, TEXT, true, false);
            t.setLetterSpacing(0.18f);
            LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams(-2, -2);
            tl.leftMargin = dp(10);
            head.addView(t, tl);
            addView(head, new LinearLayout.LayoutParams(-1, -2));
            addView(rule(LINE2), new LinearLayout.LayoutParams(-1, dp(1)));
            body.setPadding(dp(18), dp(16), dp(18), dp(18));
            addView(body, new LinearLayout.LayoutParams(-1, -2));
        }
        @Override protected void dispatchDraw(Canvas c) {
            int w = getWidth(), h = getHeight(), m = dp(14), s = dp(2);
            p.setStyle(Paint.Style.FILL);
            p.setColor(PANEL);
            c.drawRoundRect(0, 0, w, h, dp(4), dp(4), p);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(dp(1));
            p.setColor(LINE2);
            c.drawRoundRect(0.5f, 0.5f, w - 0.5f, h - 0.5f, dp(4), dp(4), p);
            super.dispatchDraw(c);
            p.setStrokeWidth(s);
            p.setColor(BLUE);
            float o = s / 2f;
            c.drawLine(o, o, m, o, p); c.drawLine(o, o, o, m, p);
            c.drawLine(w - o, h - o, w - m, h - o, p); c.drawLine(w - o, h - o, w - o, h - m, p);
        }
    }

    /** A bar under a tab: the active mark. */
    private class Underline extends Drawable {
        final Drawable back; final int color; final Paint p = new Paint();
        Underline(Drawable back, int color) { this.back = back; this.color = color; }
        @Override public void draw(Canvas c) {
            back.setBounds(getBounds()); back.draw(c);
            p.setColor(color);
            c.drawRect(getBounds().left, getBounds().bottom - dp(2), getBounds().right, getBounds().bottom, p);
        }
        @Override public void setAlpha(int a) { }
        @Override public void setColorFilter(ColorFilter f) { }
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }

    /** The slanted PLAY button's face. */
    private class Slant extends Drawable {
        final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG);
        boolean enabled = true;
        @Override public void draw(Canvas c) {
            int w = getBounds().width(), h = getBounds().height(), cut = dp(22);
            Path path = new Path();
            path.moveTo(0, 0); path.lineTo(w, 0); path.lineTo(w - cut, h); path.lineTo(0, h); path.close();
            p.setShader(new LinearGradient(0, 0, 0, h, new int[] { 0xFFCFE8FF, 0xFF5AA0FF, 0xFF2F6FE8 },
                    new float[] { 0, 0.45f, 1 }, Shader.TileMode.CLAMP));
            c.drawPath(path, p);
            edge.setStyle(Paint.Style.STROKE); edge.setStrokeWidth(dp(1)); edge.setColor(0x99BEE1FF);
            c.save(); c.clipPath(path); c.drawPath(path, edge); c.restore();
        }
        @Override public void setAlpha(int a) { }
        @Override public void setColorFilter(ColorFilter f) { }
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
        @Override public boolean isStateful() { return true; }
        @Override protected boolean onStateChange(int[] s) { invalidateSelf(); return true; }
    }

    /** A row of choices, one on: the PC launcher's .seg. */
    private class Seg extends LinearLayout {
        final TextView[] items;
        int sel;
        Seg(String[] labels) {
            super(LauncherActivity.this);
            items = new TextView[labels.length];
            setBackground(shape(0x08FFFFFF, 0x24FFFFFF, 3));
            for (int i = 0; i < labels.length; i++) {
                TextView t = text(labels[i], 12, MUTED, true, false);
                t.setLetterSpacing(0.08f);
                t.setGravity(Gravity.CENTER);
                t.setMinHeight(dp(48));
                t.setPadding(dp(6), dp(8), dp(6), dp(8));
                final int idx = i;
                t.setOnClickListener(v -> select(idx));
                items[i] = t;
                addView(t, new LinearLayout.LayoutParams(0, -2, 1));
            }
            select(0);
        }
        void select(int i) {
            sel = i;
            for (int k = 0; k < items.length; k++) {
                items[k].setTextColor(k == i ? ICE : MUTED);
                items[k].setBackground(k == i ? shape(0x402878FF, 0x00000000, 3) : null);
            }
        }
    }

    // ---------------------------------------------------------------- state

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
        renderTabs();
        String why = PackageStore.problem(this);
        JSONObject m = PackageStore.manifest(this);
        boolean ready = why == null && m != null;
        String title = m != null ? m.optString("title", "007: NightFire") : null;
        String name = title != null ? shortName(title) : "NightFire";
        heroName.setText(name.toUpperCase(Locale.ROOT));
        heroPrefix.setText("007:");
        dot.setBackground(dot(ready ? OK : m == null ? MUTED : BAD));
        status.setText(ready ? "READY TO PLAY" : m == null ? "NOT INSTALLED" : "PACKAGE NEEDS ATTENTION");
        status.setTextColor(ready ? OK : m == null ? MUTED : BAD);
        packageTitle.setText(ready ? title : m == null ? "Add your game" : "Check your package");
        String date = m != null ? m.optString("built", "") : "";
        if (date.length() >= 10) date = date.substring(0, 10);
        packageMeta.setText(ready ? "Installed on this device" + (date.isEmpty() ? "" : " · built " + date)
                : m == null ? "Import the package you created on your PC to get started." : why);
        play.setText(ready ? "PLAY " + name.toUpperCase(Locale.ROOT) : "IMPORT");
        enable(play, true);
        enable(toSettings, ready);
        importZip.setText((ready ? "Import an updated package" : "Import game package").toUpperCase(Locale.ROOT));
        settingsNote.setText(ready ? "For " + title + ". Saved to this device." : "Import a game to change its settings.");
        enable(remove, m != null);
    }

    private void renderImport() {
        boolean busy = ImportTask.active;
        enable(importZip, !busy);
        enable(importFolder, !busy);
        progress.setVisibility(busy ? View.VISIBLE : View.GONE);
        importStatus.setVisibility(busy ? View.VISIBLE : View.GONE);
        if (busy) {
            enable(play, false); enable(toSettings, false); enable(remove, false);
            play.setText("IMPORTING...");
            dot.setBackground(dot(BLUE));
            status.setText("IMPORT IN PROGRESS");
            status.setTextColor(BLUE);
            packageTitle.setText("Getting your game ready");
            packageMeta.setText("Keep the app open while your package is imported.");
            progress.setIndeterminate(ImportTask.total <= 0);
            if (ImportTask.total > 0)
                progress.setProgress((int) Math.min(1000, 1000.0 * ImportTask.done / ImportTask.total));
            importStatus.setText(ImportTask.done == 0 ? "Preparing the package..." : String.format(Locale.getDefault(),
                    ImportTask.total > 0 ? "Importing · %.0f MB of %.0f MB" : "Importing · %.0f MB copied",
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
                if (onSettings) loadSettings();
            }).setNegativeButton("Cancel", null).show();
    }

    private void loadSettings() {
        boolean ok = PackageStore.problem(this) == null;
        scaleSeg.select(indexOf(SCALES, ok ? PackageStore.readSetting(this, "render_percent") : null, 1));
        capSeg.select(indexOf(CAPS, ok ? PackageStore.readSetting(this, "fps_cap") : null, 1));
        fpsSwitch.setChecked(ok && "true".equals(PackageStore.readSetting(this, "show_fps")));
        introSwitch.setChecked(!ok || !"false".equals(PackageStore.readSetting(this, "skip_intro")));
        for (View v : new View[] { scaleSeg, capSeg, fpsSwitch, introSwitch }) { v.setEnabled(ok); v.setAlpha(ok ? 1 : 0.4f); }
        for (TextView t : scaleSeg.items) t.setEnabled(ok);
        for (TextView t : capSeg.items) t.setEnabled(ok);
        enable(save, ok);
        saveMsg.setText("");
    }

    private void saveSettings() {
        if (ImportTask.active || PackageStore.problem(this) != null) return;
        try {
            PackageStore.writeSetting(this, "render_percent", SCALES[scaleSeg.sel]);
            PackageStore.writeSetting(this, "fps_cap", CAPS[capSeg.sel]);
            PackageStore.writeSetting(this, "show_fps", fpsSwitch.isChecked() ? "true" : "false");
            PackageStore.writeSetting(this, "skip_intro", introSwitch.isChecked() ? "true" : "false");
            saveMsg.setTextColor(OK);
            saveMsg.setText("Saved.");
        } catch (Exception e) { message("Settings weren't saved", e.getMessage()); }
    }

    // ---------------------------------------------------------------- view helpers

    private int dp(float v) { return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics())); }
    private LinearLayout column() {
        LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v;
    }
    private View rule(int color) { View v = new View(this); v.setBackgroundColor(color); return v; }
    private TextView text(String value, float sp, int color, boolean bold, boolean condensed) {
        TextView t = new TextView(this);
        t.setText(value); t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp); t.setTextColor(color);
        t.setTypeface(Typeface.create(condensed ? COND : "sans-serif", bold ? Typeface.BOLD : Typeface.NORMAL));
        t.setIncludeFontPadding(false);
        t.setLineSpacing(dp(2), 1);
        return t;
    }
    /** the blue-white fire gradient over a title's height */
    private void gradient(TextView t) {
        t.addOnLayoutChangeListener((v, l, tp, r, b, ol, ot, or, ob) -> {
            float h = Math.max(1, b - tp);
            t.getPaint().setShader(new LinearGradient(0, 0, 0, h, new int[] { ICE, BLUE, DEEP },
                    new float[] { 0, 0.48f, 1 }, Shader.TileMode.CLAMP));
            t.invalidate();
        });
    }
    private LinearLayout.LayoutParams lp(int w, int h, int left, int top) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(w, h); p.leftMargin = dp(left); p.topMargin = dp(top); return p;
    }
    private void add(LinearLayout parent, View child, int gap) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2); lp.topMargin = dp(gap);
        parent.addView(child, lp);
    }
    /** label + help on top, the control below */
    private void setting(LinearLayout parent, String label, String help, View control, int gap) {
        add(parent, text(label, 14, TEXT, true, false), gap);
        add(parent, text(help, 12, MUTED, false, false), 3);
        if (control instanceof Switch) {
            // the switch sits right of the label on the PC; here it gets its own row for a big touch target
            add(parent, control, 8);
        } else add(parent, control, 10);
    }
    private Switch toggle() {
        Switch s = new Switch(this);
        s.setMinHeight(dp(48));
        s.setText("Off");
        s.setTextColor(MUTED);
        s.setOnCheckedChangeListener((b, on) -> { s.setText(on ? "On" : "Off"); s.setTextColor(on ? ICE : MUTED); });
        ColorStateList thumb = new ColorStateList(new int[][] { { android.R.attr.state_checked }, {} },
                new int[] { 0xFFEEF6FF, 0xFF9AA8C0 });
        ColorStateList track = new ColorStateList(new int[][] { { android.R.attr.state_checked }, {} },
                new int[] { BLUE, 0xFF1B3266 });
        s.setThumbTintList(thumb);
        s.setTrackTintList(track);
        return s;
    }
    private GradientDrawable shape(int fill, int stroke, int radius) {
        GradientDrawable d = new GradientDrawable(); d.setColor(fill); d.setCornerRadius(dp(radius));
        d.setStroke(dp(1), stroke); return d;
    }
    private GradientDrawable dot(int color) {
        GradientDrawable d = new GradientDrawable(); d.setShape(GradientDrawable.OVAL); d.setColor(color); return d;
    }
    private Button base(String label) {
        Button b = new Button(this);
        b.setText(label.toUpperCase(Locale.ROOT)); b.setAllCaps(false); b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        b.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        b.setLetterSpacing(0.1f);
        b.setSingleLine(false); b.setMaxLines(3); b.setEllipsize(null);
        b.setMinHeight(dp(52)); b.setMinimumWidth(0); b.setMinWidth(0);
        b.setPadding(dp(16), dp(12), dp(16), dp(12));
        b.setStateListAnimator(null);
        return b;
    }
    private Button primary(String label) {
        Button b = base(label);
        b.setTextColor(ICE);
        b.setBackground(focusable(new int[] { 0x382878FF, 0x2E1D4FC4 }, 0x8C2878FF, 0x662878FF));
        return b;
    }
    private Button ghost(String label) {
        Button b = base(label);
        b.setTextColor(0xFFC4D6F0);
        b.setBackground(focusable(new int[] { 0x08FFFFFF, 0x08FFFFFF }, 0x29FFFFFF, 0x66FFFFFF));
        return b;
    }
    private Drawable focusable(int[] fill, int stroke, int focus) {
        android.graphics.drawable.StateListDrawable s = new android.graphics.drawable.StateListDrawable();
        GradientDrawable f = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, fill);
        f.setCornerRadius(dp(3)); f.setStroke(dp(2), GOLD);
        s.addState(new int[] { android.R.attr.state_focused }, f);
        s.addState(new int[] { android.R.attr.state_pressed }, pressed(fill, stroke));
        GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, fill);
        d.setCornerRadius(dp(3)); d.setStroke(dp(1), stroke);
        s.addState(new int[] {}, d);
        return s;
    }
    private Drawable pressed(int[] fill, int stroke) {
        GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] { fill[0] | 0x40000000, fill[1] | 0x40000000 });
        d.setCornerRadius(dp(3)); d.setStroke(dp(1), stroke | 0xFF000000);
        return d;
    }
    private void styleSlant(Button b) {
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 26);
        b.setTypeface(Typeface.create(COND, Typeface.BOLD));
        b.setLetterSpacing(0.22f);
        b.setTextColor(0xFF06183F);
        b.setSingleLine(true);
        b.setMinWidth(dp(220)); b.setMinimumWidth(dp(220));
        b.setStateListAnimator(null);
        b.setPadding(dp(36), 0, dp(54), 0);
        b.setBackground(new Slant());
    }
    private void enable(Button button, boolean enabled) { button.setEnabled(enabled); button.setAlpha(enabled ? 1 : 0.4f); }

    private static int indexOf(String[] values, String value, int fallback) {
        for (int i = 0; value != null && i < values.length; i++) if (values[i].equals(value)) return i;
        return fallback;
    }
    private void message(String title, String body) {
        new AlertDialog.Builder(this).setTitle(title).setMessage(body).setPositiveButton("OK", null).show();
    }
}
