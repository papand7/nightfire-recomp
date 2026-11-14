package io.github.papand7.nightfire;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;

import java.io.File;

/**
 * The game's relaunch into another image (GameActivity.relaunch): in the launcher's process, it waits until the game's
 * process has ended (it ends itself; killed after two seconds), then starts the game activity again with the library
 * and environment it was given.
 */
public class RestartActivity extends Activity {
    private int waited = 0;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        final int pid = getIntent().getIntExtra(GameActivity.EXTRA_PID, 0);
        final Handler handler = new Handler(Looper.getMainLooper());
        handler.post(new Runnable() {
            @Override
            public void run() {
                boolean alive = pid > 0 && new File("/proc/" + pid).exists();
                if (alive && waited == 2000) android.os.Process.killProcess(pid);
                if (alive && waited < 4000) {
                    waited += 50;
                    handler.postDelayed(this, 50);
                    return;
                }
                Intent i = new Intent(RestartActivity.this, GameActivity.class);
                i.putExtra(GameActivity.EXTRA_GAME, getIntent().getStringExtra(GameActivity.EXTRA_GAME));
                i.putExtra(GameActivity.EXTRA_LIB,getIntent().getStringExtra(GameActivity.EXTRA_LIB));
                i.putExtra(GameActivity.EXTRA_ENV, getIntent().getStringArrayExtra(GameActivity.EXTRA_ENV));
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                startActivity(i);
                finish();
                overridePendingTransition(0, 0);
            }
        });
    }
}
