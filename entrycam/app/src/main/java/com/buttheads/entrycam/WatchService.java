package com.buttheads.entrycam;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

/**
 * Brings the camera back on screen:
 *  - when the TV wakes up (Fire TV goes to its home screen after standby), and
 *  - when someone left the camera (Home button) and walked away for 10 minutes.
 */
public class WatchService extends Service {
    private static final long CHECK_MS = 60_000;
    private static final long AWAY_MS = 10 * 60_000;
    private static final String CHANNEL = "entrycam";

    private final Handler handler = new Handler(Looper.getMainLooper());

    static void start(Context c) {
        Intent i = new Intent(c, WatchService.class);
        try {
            if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i);
            else c.startService(i);
        } catch (Exception ignored) {
            // Some devices refuse; the camera still works, it just won't come back by itself.
        }
    }

    static void showCamera(Context c) {
        Intent i = new Intent(c, MainActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        try {
            c.startActivity(i);
        } catch (Exception ignored) {
        }
    }

    private final BroadcastReceiver wake = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            // Give the Fire TV home screen a moment to finish loading, then take over.
            handler.postDelayed(() -> {
                if (!MainActivity.visible) showCamera(WatchService.this);
            }, 4_000);
        }
    };

    private final Runnable check = new Runnable() {
        @Override
        public void run() {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            boolean screenOn = pm == null || pm.isInteractive();
            if (screenOn && !MainActivity.visible && MainActivity.hiddenSince > 0
                    && System.currentTimeMillis() - MainActivity.hiddenSince > AWAY_MS) {
                showCamera(WatchService.this);
            }
            handler.postDelayed(this, CHECK_MS);
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        goForeground();
        IntentFilter f = new IntentFilter();
        f.addAction(Intent.ACTION_SCREEN_ON);
        f.addAction(Intent.ACTION_USER_PRESENT);
        f.addAction(Intent.ACTION_DREAMING_STOPPED);
        registerReceiver(wake, f);
        handler.postDelayed(check, CHECK_MS);
    }

    private void goForeground() {
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            nm.createNotificationChannel(new NotificationChannel(
                    CHANNEL, "Entry Cam", NotificationManager.IMPORTANCE_MIN));
            b = new Notification.Builder(this, CHANNEL);
        } else {
            b = new Notification.Builder(this);
        }
        Notification n = b.setContentTitle("Entry Cam")
                .setContentText("Keeping the camera on screen")
                .setSmallIcon(R.drawable.icon)
                .setOngoing(true)
                .build();
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(1, n);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        try { unregisterReceiver(wake); } catch (Exception ignored) { }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
