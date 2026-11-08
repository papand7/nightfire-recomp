package io.github.papand7.nightfire;

import android.content.Intent;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;

/**
 * SDL's activity around the game package's library. The library (SDL's C side linked in) is loaded from the app's
 * private storage, not from the APK: the package was built on the player's PC from their own disc. Which image's
 * library runs comes with the intent (a relaunch into the other xbe), else the package's main one.
 */
public class GameActivity extends SDLActivity {
    static final String TAG = "NightFire";
    static final String EXTRA_LIB = "lib";
    static final String EXTRA_ENV = "env";
    static final String EXTRA_PID = "pid";

    private String library() {
        String lib = getIntent() != null ? getIntent().getStringExtra(EXTRA_LIB) : null;
        if (lib == null || lib.contains("/")) lib = PackageStore.mainLibrary(this);
        return new File(PackageStore.gameDir(this), lib).getAbsolutePath();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };  // not loaded by name: loadLibraries below
    }

    @Override
    public void loadLibraries() {
        String path = library();
        Log.v(TAG, "loading " + path);
        System.load(path);
    }

    @Override
    protected String getMainSharedObject() {
        return library();
    }

    @Override
    protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<>();
        String[] env = getIntent() != null ? getIntent().getStringArrayExtra(EXTRA_ENV) : null;
        if (env != null) for (String e : env) args.add("--env=" + e);
        return args.toArray(new String[0]);
    }

    /**
     * Called by the runtime (app::AndroidRelaunch) when the game switches to another image (kernel::Relaunch): the
     * restart activity in the launcher's process waits for this process to end, then starts this activity again with
     * that library and the environment handed over. The runtime ends this process right after.
     */
    public void relaunch(String lib, String[] env) {
        Log.v(TAG, "relaunch into " + lib);
        Intent i = new Intent(this, RestartActivity.class);
        i.putExtra(EXTRA_LIB, lib);
        i.putExtra(EXTRA_ENV, env);
        i.putExtra(EXTRA_PID, android.os.Process.myPid());
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
    }
}
