package org.viewtouch.pos

import android.app.Activity
import android.content.Context
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.view.WindowManager

// Find a Tablet: another screen of the store is looking for this tablet (ui/cpp/findalarm.cpp).
// Loud beeps on the ALARM stream, so they're heard with the media volume down: the alarm volume is
// turned all the way up for it and put back as it was afterwards. The screen is woken and kept on
// while it beeps. Stops with stop() (tapped here, or Stop on the other screen) or after `seconds`.
// The app must be running to hear the store: a tablet whose screen was turned off with the power
// button (Android pauses the app, and the store drops it after 15 s) shows as not connected there.
object FindMe {
    private val main = Handler(Looper.getMainLooper())
    private var tone: ToneGenerator? = null
    private var wake: PowerManager.WakeLock? = null
    private var audio: AudioManager? = null
    private var oldVolume = -1
    private var keptOn = false        // FLAG_KEEP_SCREEN_ON added here (else ViewTouch had it already)
    private var activity: Activity? = null
    private var beeping = false

    private val beep = object : Runnable {
        override fun run() {
            if (!beeping) return
            try {
                tone?.startTone(ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD, 700)
            } catch (_: Exception) {
            }
            main.postDelayed(this, 1000)
        }
    }
    private val timeUp = Runnable { stopNow() }

    @JvmStatic
    fun start(activity: Activity, seconds: Int) {
        this.activity = activity
        wakeScreen(activity, seconds)
        try {
            val am = activity.getSystemService(Context.AUDIO_SERVICE) as AudioManager
            audio = am
            if (oldVolume < 0)
                oldVolume = am.getStreamVolume(AudioManager.STREAM_ALARM)
            am.setStreamVolume(AudioManager.STREAM_ALARM, am.getStreamMaxVolume(AudioManager.STREAM_ALARM), 0)
        } catch (_: Exception) {
            // Not allowed (Do Not Disturb on some tablets): it beeps at the alarm volume set.
        }
        if (tone == null) {
            tone = try {
                ToneGenerator(AudioManager.STREAM_ALARM, ToneGenerator.MAX_VOLUME)
            } catch (_: Exception) {
                null
            }
        }
        beeping = true
        main.removeCallbacks(beep)
        main.post(beep)
        main.removeCallbacks(timeUp)
        main.postDelayed(timeUp, seconds.coerceIn(5, 600) * 1000L)
    }

    @JvmStatic
    fun stop(activity: Activity) {
        this.activity = activity
        stopNow()
    }

    private fun stopNow() {
        beeping = false
        main.removeCallbacks(beep)
        main.removeCallbacks(timeUp)
        try {
            tone?.stopTone()
            tone?.release()
        } catch (_: Exception) {
        }
        tone = null
        if (oldVolume >= 0) {
            try {
                audio?.setStreamVolume(AudioManager.STREAM_ALARM, oldVolume, 0)
            } catch (_: Exception) {
            }
            oldVolume = -1
        }
        wake?.let { if (it.isHeld) it.release() }
        wake = null
        activity?.let { a ->
            try {
                if (keptOn)
                    a.window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                    a.setTurnScreenOn(false)
                    a.setShowWhenLocked(false)
                }
            } catch (_: Exception) {
            }
        }
        keptOn = false
    }

    // The screen on (a dimmed or sleeping screen woken; over a lock screen too) and kept on.
    private fun wakeScreen(activity: Activity, seconds: Int) {
        try {
            val window = activity.window
            if ((window.attributes.flags and WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) == 0) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                keptOn = true
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                activity.setTurnScreenOn(true)
                activity.setShowWhenLocked(true)
            }
            val pm = activity.getSystemService(Context.POWER_SERVICE) as PowerManager
            if (wake?.isHeld != true) {
                @Suppress("DEPRECATION")
                val lock = pm.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK or PowerManager.ACQUIRE_CAUSES_WAKEUP,
                                          "ViewTouch:findMe")
                lock.acquire(seconds.coerceIn(5, 600) * 1000L + 5_000L)
                wake = lock
            }
        } catch (_: Exception) {
        }
    }
}
