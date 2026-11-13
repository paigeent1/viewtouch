package org.viewtouch.pos

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.util.Log
import android.view.WindowManager
import java.io.File

// A billboard: a TV (Fire TV, Android TV) showing the store's guest-screen slides (Billboard.qml).
// Not a Device Owner like the tablets (Kiosk.kt): an ordinary app the TV starts.
object Billboard {
    private var wake: PowerManager.WakeLock? = null

    // main.cpp keeps "billboard" here once the store has said this screen is one.
    fun isBillboard(context: Context): Boolean = try {
        File(context.filesDir, "screen-role.txt").readText().trim() == "billboard"
    } catch (_: Exception) {
        false
    }

    // Outside its hours (dark): the screen may sleep, and Fire OS turns the TV off by its own
    // sleep timer (an ordinary app can't put the TV to sleep itself: that needs the system's
    // INJECT_EVENTS / DEVICE_POWER). Back inside them: kept on, and Android asked to wake the
    // screen (a wake lock that turns it on; a TV in deep standby may not run the app to do it).
    @JvmStatic
    fun dark(activity: Activity, dark: Boolean) {
        try {
            val window = activity.window
            if (dark) {
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                wake?.let { if (it.isHeld) it.release() }
                wake = null
                return
            }
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1)
                activity.setTurnScreenOn(true)
            val pm = activity.getSystemService(Context.POWER_SERVICE) as PowerManager
            if (!pm.isInteractive) {
                @Suppress("DEPRECATION")
                val lock = pm.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK or PowerManager.ACQUIRE_CAUSES_WAKEUP,
                                          "ViewTouch:billboard")
                lock.acquire(10_000L)   // long enough to turn on; FLAG_KEEP_SCREEN_ON keeps it on
                wake = lock
            }
        } catch (_: Exception) {
        }
    }

    // In front right now (ViewTouchApplication sets it): the boot start below doesn't open it again.
    @Volatile
    @JvmStatic
    var inFront = false

    private var booted = false

    // The TV turned on (a power cut, a restart): a billboard opens again by itself, with no remote.
    // Android 10+ (Fire OS 8, Android TV) refuses an app opening itself from the background unless it
    // may "display over other apps": vt-add-billboard allows that once over adb
    // (appops set org.viewtouch.pos SYSTEM_ALERT_WINDOW allow). Fire OS 7 (Android 9) needs nothing.
    // The TV's home screen comes up after this and may cover it, so it's opened again a little later
    // while it isn't in front. Never while the screen is off: the store put the TV to sleep (outside its
    // hours, --sleep) and that stays so. Inside or outside its hours, the store then says on or dark.
    fun boot(context: Context) {
        if (!isBillboard(context) || booted)
            return
        booted = true
        val app = context.applicationContext
        val overlay = Build.VERSION.SDK_INT < Build.VERSION_CODES.M || Settings.canDrawOverlays(app)
        Log.i(TAG, "Billboard: the TV turned on; opening the billboard (may start by itself: $overlay)")
        open(app)
        val main = Handler(Looper.getMainLooper())
        for (delay in longArrayOf(10_000L, 25_000L, 45_000L))
            main.postDelayed({ if (!inFront && awake(app)) open(app) }, delay)
    }

    private const val TAG = "ViewTouch"

    private fun awake(context: Context): Boolean = try {
        (context.getSystemService(Context.POWER_SERVICE) as PowerManager).isInteractive
    } catch (_: Exception) {
        true
    }

    private fun open(context: Context) {
        try {
            val pm = context.packageManager
            val start = pm.getLaunchIntentForPackage(context.packageName)
                ?: pm.getLeanbackLaunchIntentForPackage(context.packageName) ?: return
            context.startActivity(start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED))
        } catch (e: Exception) {
            Log.w(TAG, "Billboard: couldn't open the billboard at start: $e")
        }
    }
}
