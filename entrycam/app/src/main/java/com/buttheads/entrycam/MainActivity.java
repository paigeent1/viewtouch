package com.buttheads.entrycam;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.TextView;

import androidx.media3.common.MediaItem;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.exoplayer.DefaultLoadControl;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.rtsp.RtspMediaSource;
import androidx.media3.ui.PlayerView;

/**
 * Entry Cam: shows one RTSP camera full screen, muted, forever.
 *
 * The camera URL is never in the code (the repo is public). It is stored on the device:
 *  - set from the Pi:  adb shell am start -n com.buttheads.entrycam/.MainActivity --es url "rtsp://..."
 *  - or on screen:     Menu button on the remote (or long-press on a touch screen)
 */
public class MainActivity extends Activity {
    static final String PREFS = "entrycam";
    static final String KEY_URL = "camera_url";
    static final String URL_HINT = "rtsp://192.168.1.1:7447/";

    private static final long RETRY_MS = 5_000;      // wait before reconnecting
    private static final long STALL_MS = 20_000;     // picture frozen this long = reconnect
    private static final long WATCH_MS = 5_000;

    /** Read by WatchService to know whether the camera is on screen. */
    static volatile boolean visible = false;
    static volatile long hiddenSince = 0;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private ExoPlayer player;
    private PlayerView playerView;
    private TextView status;
    private long lastPosition = -1;
    private long lastProgressAt = 0;
    private boolean retryPending = false;
    private AlertDialog setupDialog;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
                | WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON);
        setContentView(R.layout.main);
        playerView = findViewById(R.id.player);
        status = findViewById(R.id.status);
        playerView.setOnLongClickListener(v -> { showSetup(); return true; });
        takeUrlFromIntent(getIntent());
        WatchService.start(this);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (takeUrlFromIntent(intent) && player != null) {
            restart("Camera changed");
        }
    }

    /** Saves an url passed with --es url "...". Returns true if one was given. */
    private boolean takeUrlFromIntent(Intent intent) {
        if (intent == null) return false;
        String url = intent.getStringExtra("url");
        if (url == null || url.trim().isEmpty()) return false;
        prefs(this).edit().putString(KEY_URL, url.trim()).apply();
        return true;
    }

    static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    private String url() {
        return prefs(this).getString(KEY_URL, "");
    }

    @Override
    protected void onStart() {
        super.onStart();
        visible = true;
        hideSystemBars();
        if (url().isEmpty()) {
            showSetup();
        } else {
            startPlayer();
        }
        handler.postDelayed(watchdog, WATCH_MS);
    }

    @Override
    protected void onStop() {
        super.onStop();
        visible = false;
        hiddenSince = System.currentTimeMillis();
        handler.removeCallbacksAndMessages(null);
        retryPending = false;
        releasePlayer();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    // ---- playback -------------------------------------------------------------

    private void startPlayer() {
        releasePlayer();
        String url = url();
        if (url.isEmpty()) return;
        showStatus("Connecting…");

        // Small buffers: a camera should be live, not a few seconds behind.
        DefaultLoadControl loadControl = new DefaultLoadControl.Builder()
                .setBufferDurationsMs(500, 2_000, 250, 500)
                .build();
        player = new ExoPlayer.Builder(this).setLoadControl(loadControl).build();
        player.setVolume(0f);
        player.addListener(listener);
        playerView.setPlayer(player);

        RtspMediaSource source = new RtspMediaSource.Factory()
                .setForceUseRtpTcp(true)     // TCP: steadier over Wi-Fi than UDP
                .setTimeoutMs(8_000)
                .createMediaSource(MediaItem.fromUri(Uri.parse(url)));
        player.setMediaSource(source);
        player.setPlayWhenReady(true);
        player.prepare();
        lastPosition = -1;
        lastProgressAt = System.currentTimeMillis();
    }

    private void releasePlayer() {
        if (player != null) {
            player.removeListener(listener);
            player.release();
            player = null;
        }
        if (playerView != null) playerView.setPlayer(null);
    }

    private void restart(String why) {
        if (retryPending) return;
        retryPending = true;
        releasePlayer();
        showStatus(why + " — reconnecting…");
        handler.postDelayed(() -> {
            retryPending = false;
            if (visible) startPlayer();
        }, RETRY_MS);
    }

    private final Player.Listener listener = new Player.Listener() {
        @Override
        public void onPlaybackStateChanged(int state) {
            if (state == Player.STATE_READY) {
                hideStatus();
                lastProgressAt = System.currentTimeMillis();
            } else if (state == Player.STATE_ENDED) {
                restart("Stream ended");
            }
        }

        @Override
        public void onPlayerError(PlaybackException error) {
            restart("No signal");
        }
    };

    /** Catches a picture that freezes without an error (Wi-Fi hiccup, UDM restart). */
    private final Runnable watchdog = new Runnable() {
        @Override
        public void run() {
            if (player != null && !retryPending) {
                long pos = player.getCurrentPosition();
                long now = System.currentTimeMillis();
                if (pos != lastPosition) {
                    lastPosition = pos;
                    lastProgressAt = now;
                } else if (now - lastProgressAt > STALL_MS) {
                    restart("Picture froze");
                }
            }
            handler.postDelayed(this, WATCH_MS);
        }
    };

    private void showStatus(String text) {
        status.setText(text);
        status.setVisibility(View.VISIBLE);
    }

    private void hideStatus() {
        status.setVisibility(View.GONE);
    }

    // ---- setup ----------------------------------------------------------------

    private void showSetup() {
        if (setupDialog != null && setupDialog.isShowing()) return;
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        String current = url();
        input.setText(current.isEmpty() ? URL_HINT : current);
        input.setSelection(input.getText().length());
        setupDialog = new AlertDialog.Builder(this)
                .setTitle("Entry Cam — camera address")
                .setMessage("UniFi Protect RTSP link (plain rtsp, port 7447).")
                .setView(input)
                .setPositiveButton("Save", (d, w) -> {
                    String u = input.getText().toString().trim();
                    if (!u.isEmpty() && !u.equals(URL_HINT)) {
                        prefs(this).edit().putString(KEY_URL, u).apply();
                    }
                    startPlayer();
                })
                .setNegativeButton("Cancel", (d, w) -> startPlayer())
                .create();
        setupDialog.show();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_MENU || keyCode == KeyEvent.KEYCODE_SETTINGS) {
            showSetup();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            return true;   // Back does nothing: the camera stays up. Home still leaves.
        }
        return super.onKeyDown(keyCode, event);
    }
}
