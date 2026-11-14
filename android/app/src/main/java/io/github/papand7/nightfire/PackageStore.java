package io.github.papand7.nightfire;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The game packages in the app's private storage, a folder a game (files/games/<short>, short from the package's
 * package.json: NightFire, AgentUnderFire): its libraries, the disc's files in assets/, and what the game keeps beside
 * them as beside an exe (settings.toml, hdd/ with the saves, logs/, the shader caches). Imported from a .zip or a folder
 * the player picks; the player's own data stays across imports of the same game. The launcher shows one game at a time
 * (the current one: the last imported or picked).
 */
final class PackageStore {
    private PackageStore() {}

    /** what the game keeps beside its libraries: moved into a newly imported package */
    private static final String[] KEPT = { "settings.toml", "hdd", "hdd2", "logs", "shadercache.bin",
                                           "pipelinecache_vk.bin", "Screenshots" };

    interface Progress {
        void update(long done, long total, String what);
    }

    private static final String PREFS = "packages", CURRENT = "current";

    private static File gamesDir(Context c) {
        File games = new File(c.getFilesDir(), "games");
        // the single package of earlier versions (files/game, NightFire's): moved to its game's folder
        File legacy = new File(c.getFilesDir(), "game");
        if (legacy.isDirectory()) {
            JSONObject m = read(new File(legacy, "package.json"));
            String id = idOf(m != null ? m.optString("short", "") : "");
            games.mkdirs();
            File to = new File(games, id.isEmpty() ? "NightFire" : id);
            if (!to.exists() && legacy.renameTo(to)) setCurrent(c, to.getName());
        }
        games.mkdirs();
        return games;
    }

    /** a package's folder name: its short name, letters, digits, - and _ only */
    private static String idOf(String shortName) {
        return shortName.replaceAll("[^A-Za-z0-9_-]", "");
    }

    /** the installed games (their folders' names), each with a package.json */
    static List<String> games(Context c) {
        ArrayList<String> ids = new ArrayList<>();
        File[] dirs = gamesDir(c).listFiles();
        if (dirs != null)
            for (File d : dirs)
                if (d.isDirectory() && new File(d, "package.json").isFile()) ids.add(d.getName());
        Collections.sort(ids);
        return ids;
    }

    /** the game the launcher shows: the one picked or imported last, else the first installed, else null */
    static String current(Context c) {
        List<String> ids = games(c);
        String id = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(CURRENT, null);
        if (id != null && ids.contains(id)) return id;
        return ids.isEmpty() ? null : ids.get(0);
    }

    static void setCurrent(Context c, String id) {
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(CURRENT, id).commit();
    }

    /** the current game's folder (files/games/none while none is installed) */
    static File gameDir(Context c) {
        return gameDir(c, current(c));
    }

    static File gameDir(Context c, String id) {
        return new File(gamesDir(c), id == null || id.isEmpty() || id.contains("/") || id.startsWith(".") ? "none" : id);
    }

    static JSONObject manifest(Context c) {
        return read(new File(gameDir(c), "package.json"));
    }

    static JSONObject manifest(Context c, String id) {
        return read(new File(gameDir(c, id), "package.json"));
    }

    /** a game removed: its package and the player's data beside it (saves, settings) */
    static void remove(Context c, String id) {
        if (games(c).contains(id)) deleteTree(gameDir(c, id));
    }

    private static JSONObject read(File f) {
        if (!f.isFile()) return null;
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), 1 << 20)];
            int n = 0;
            for (int r; n < b.length && (r = in.read(b, n, b.length - n)) > 0;) n += r;
            return new JSONObject(new String(b, 0, n, StandardCharsets.UTF_8));
        } catch (Exception e) {
            return null;
        }
    }

    static String mainLibrary(Context c, String id) {
        JSONObject m = manifest(c, id);
        String lib = m != null ? m.optString("main", "") : "";
        return lib.isEmpty() || lib.contains("/") ? "libNightfire.so" : lib;
    }

    /** null when the package can run here, else why not */
    static String problem(Context c) {
        JSONObject m = manifest(c);
        if (m == null) return "No game package imported yet.";
        return problem(m, gameDir(c));
    }

    private static String problem(JSONObject m, File dir) {
        String abi = m.optString("abi", "");
        boolean abiOk = false;
        for (String a : Build.SUPPORTED_ABIS) abiOk |= a.equals(abi);
        if (!abiOk) return "The package is built for " + abi + "; this device runs " + Build.SUPPORTED_ABIS[0] + ".";
        String sdl = m.optString("sdl", "");
        if (!sdl.equals(BuildConfig.SDL_VERSION))
            return "The package was built with SDL " + sdl + ", this app has SDL " + BuildConfig.SDL_VERSION
                   + ": build the package again with the launcher, or update the app.";
        JSONArray libs = m.optJSONArray("libraries");
        if (libs == null || libs.length() == 0) return "The package lists no game libraries.";
        for (int i = 0; i < libs.length(); i++)
            if (!new File(dir, libs.optString(i)).isFile()) return "The package lacks " + libs.optString(i) + ".";
        if (!new File(dir, "assets").isDirectory()) return "The package has no assets folder (the game's disc).";
        return null;
    }

    // ---- import

    /** a .zip package: unpacked beside the current one, checked, then put in its place */
    static void importZip(Context c, Uri uri, Progress p) throws IOException {
        ContentResolver cr = c.getContentResolver();
        long total = sizeOf(cr, uri);
        File work = fresh(c);
        checkSpace(c, total);
        long done = 0;
        byte[] buf = new byte[1 << 20];
        InputStream raw = cr.openInputStream(uri);
        if (raw == null) throw new IOException("the file could not be opened");
        try (ZipInputStream zip = new ZipInputStream(raw)) {
            for (ZipEntry e; (e = zip.getNextEntry()) != null;) {
                File out = inside(work, e.getName());
                if (e.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(out)) {
                    for (int n; (n = zip.read(buf)) > 0;) {
                        o.write(buf, 0, n);
                        done += n;
                        if (p != null) p.update(done, total, e.getName());
                    }
                }
            }
        }
        install(c, work);
    }

    /** a package folder (the zip unpacked, or the launcher's output folder) */
    static void importTree(Context c, Uri tree, Progress p) throws IOException {
        File work = fresh(c);
        String root = DocumentsContract.getTreeDocumentId(tree);
        long[] total = { 0 };
        measure(c.getContentResolver(), tree, root, total);
        checkSpace(c, total[0]);
        long[] done = { 0 };
        copyTree(c.getContentResolver(), tree, root, work, new byte[1 << 20], done, total[0], p);
        install(c, work);
    }

    private static File fresh(Context c) throws IOException {
        File work = new File(c.getFilesDir(), "games.import");
        deleteTree(work);
        if (!work.mkdirs()) throw new IOException("no room for the import in the app's storage");
        return work;
    }

    private static void install(Context c, File work) throws IOException {
        JSONObject m;
        try (InputStream in = new FileInputStream(new File(work, "package.json"))) {
            byte[] b = new byte[64 * 1024];
            int n = 0;
            for (int r; n < b.length && (r = in.read(b, n, b.length - n)) > 0;) n += r;
            m = new JSONObject(new String(b, 0, n, StandardCharsets.UTF_8));
        } catch (Exception e) {
            deleteTree(work);
            throw new IOException("not a game package (no package.json): build one with the launcher on your PC");
        }
        String why = problem(m, work);
        if (why != null) {
            deleteTree(work);
            throw new IOException(why);
        }
        // the libraries read-only (Android's rule for loaded code), the player's data from the old package kept
        File[] files = work.listFiles();
        if (files != null)
            for (File f : files)
                if (f.getName().endsWith(".so")) f.setReadOnly();
        // its game's folder: the package's short name (the first NightFire packages had none)
        String id = idOf(m.optString("short", ""));
        if (id.isEmpty()) id = "NightFire";
        File game = gameDir(c, id);
        if (game.isDirectory())
            for (String k : KEPT) {
                File from = new File(game, k), to = new File(work, k);
                if (from.exists() && !to.exists() && !from.renameTo(to)) throw new IOException("could not keep " + k);
            }
        deleteTree(game);
        if (!work.renameTo(game)) throw new IOException("could not put the package in place");
        setCurrent(c, id);
    }

    private static File inside(File dir, String name) throws IOException {
        File f = new File(dir, name);
        String d = dir.getCanonicalPath() + File.separator;
        if (!f.getCanonicalPath().startsWith(d)) throw new IOException("bad entry in the package: " + name);
        return f;
    }

    private static long sizeOf(ContentResolver cr, Uri uri) {
        try (Cursor q = cr.query(uri, new String[] { OpenableColumns.SIZE }, null, null, null)) {
            if (q != null && q.moveToFirst() && !q.isNull(0)) return q.getLong(0);
        } catch (Exception ignored) {
        }
        return -1;
    }

    private static void checkSpace(Context c, long need) throws IOException {
        if (need <= 0) return;
        long free = new StatFs(c.getFilesDir().getPath()).getAvailableBytes();
        if (free < need + (256L << 20))
            throw new IOException(String.format("not enough free space: the package needs %.1f GB, %.1f GB are free",
                                                need / 1e9, free / 1e9));
    }

    private static final String[] COLUMNS = { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                                              DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                                              DocumentsContract.Document.COLUMN_MIME_TYPE,
                                              DocumentsContract.Document.COLUMN_SIZE };

    private static void measure(ContentResolver cr, Uri tree, String doc, long[] total) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, doc);
        try (Cursor q = cr.query(children, COLUMNS, null, null, null)) {
            while (q != null && q.moveToNext()) {
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(q.getString(2))) measure(cr, tree, q.getString(0), total);
                else total[0] += q.isNull(3) ? 0 : q.getLong(3);
            }
        }
    }

    private static void copyTree(ContentResolver cr, Uri tree, String doc, File dir, byte[] buf, long[] done, long total,
                                 Progress p) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, doc);
        try (Cursor q = cr.query(children, COLUMNS, null, null, null)) {
            while (q != null && q.moveToNext()) {
                String id = q.getString(0), name = q.getString(1);
                File out = inside(dir, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(q.getString(2))) {
                    out.mkdirs();
                    copyTree(cr, tree, id, out, buf, done, total, p);
                    continue;
                }
                try (InputStream in = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
                     OutputStream o = new FileOutputStream(out)) {
                    if (in == null) throw new IOException("could not read " + name);
                    for (int n; (n = in.read(buf)) > 0;) {
                        o.write(buf, 0, n);
                        done[0] += n;
                        if (p != null) p.update(done[0], total, name);
                    }
                }
            }
        }
    }

    static void deleteTree(File f) {
        if (!f.exists()) return;
        File[] list = f.isDirectory() ? f.listFiles() : null;
        if (list != null)
            for (File x : list) deleteTree(x);
        f.delete();
    }

    // ---- settings.toml: a few top-level keys the launcher sets (the game's menus have the rest)

    static String readSetting(Context c, String key) {
        for (String line : settingsLines(c)) {
            if (line.startsWith("[")) break;
            String t = line.trim();
            if (t.startsWith(key) && t.substring(key.length()).trim().startsWith("="))
                return t.substring(t.indexOf('=') + 1).trim();
        }
        return null;
    }

    static void writeSetting(Context c, String key, String value) throws IOException {
        java.util.List<String> lines = settingsLines(c);
        int top = lines.size();
        for (int i = 0; i < lines.size(); i++) {
            String t = lines.get(i).trim();
            if (t.startsWith("[")) {
                top = i;
                break;
            }
            if (t.startsWith(key) && t.substring(key.length()).trim().startsWith("=")) {
                lines.set(i, key + " = " + value);
                writeLines(c, lines);
                return;
            }
        }
        lines.add(Math.min(top, lines.size()), key + " = " + value);
        writeLines(c, lines);
    }

    private static java.util.List<String> settingsLines(Context c) {
        java.util.ArrayList<String> lines = new java.util.ArrayList<>();
        File f = new File(gameDir(c), "settings.toml");
        if (!f.isFile()) return lines;
        try (java.io.BufferedReader r = new java.io.BufferedReader(
                 new java.io.InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            for (String l; (l = r.readLine()) != null;) lines.add(l);
        } catch (IOException ignored) {
        }
        return lines;
    }

    private static void writeLines(Context c, java.util.List<String> lines) throws IOException {
        File dir = gameDir(c);
        if (!dir.isDirectory()) throw new IOException("import a game package first");
        try (OutputStream o = new FileOutputStream(new File(dir, "settings.toml"))) {
            for (String l : lines) o.write((l + "\n").getBytes(StandardCharsets.UTF_8));
        }
    }
}
