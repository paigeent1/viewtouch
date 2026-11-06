package org.viewtouch.pos

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.PowerManager
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

    // The TV turned on: a billboard starts again (Android 10+ may refuse an app starting itself
    // from the background; Fire OS 7 is Android 9 and lets it).
    fun boot(context: Context) {
        if (!isBillboard(context))
            return
        try {
            val pm = context.packageManager
            val start = pm.getLaunchIntentForPackage(context.packageName)
                ?: pm.getLeanbackLaunchIntentForPackage(context.packageName) ?: return
            context.startActivity(start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        } catch (_: Exception) {
        }
    }
}
