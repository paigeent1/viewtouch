package org.viewtouch.pos

import android.app.Activity
import android.app.admin.DeviceAdminReceiver
import android.app.admin.DevicePolicyManager
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.UserManager

// A tablet only for ViewTouch, like the store's register: ViewTouch is the
// tablet's home screen (it starts when the tablet turns on), pinned (no
// Home, Recents or notifications; the power menu stays for a restart), and
// no lock screen. Only on a tablet set up for it once, over a USB cable:
//   adb shell dpm set-device-owner org.viewtouch.pos/.KioskAdmin
// Elsewhere (phones, a Stripe reader) nothing here does anything.
// Manager -> Close ViewTouch lets go until the tablet restarts.
object Kiosk {
    private fun admin(context: Context) = ComponentName(context, KioskAdmin::class.java)
    // ViewTouch as a home screen: an entry that's off except on a locked tablet
    // (phones never offer ViewTouch as their home screen).
    private fun home(context: Context) = ComponentName(context.packageName, "org.viewtouch.pos.Home")

    private fun setHome(context: Context, on: Boolean) {
        context.packageManager.setComponentEnabledSetting(home(context),
            if (on) PackageManager.COMPONENT_ENABLED_STATE_ENABLED else PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
            PackageManager.DONT_KILL_APP)
    }

    private fun policy(context: Context): DevicePolicyManager? {
        val dpm = context.getSystemService(Context.DEVICE_POLICY_SERVICE) as DevicePolicyManager
        return if (dpm.isDeviceOwnerApp(context.packageName)) dpm else null
    }

    @JvmStatic
    fun isLocked(context: Context): Boolean = policy(context) != null

    // ViewTouch as the home screen, pinned. Called each time it comes to the front.
    @JvmStatic
    fun apply(activity: Activity) {
        val dpm = policy(activity) ?: return
        val who = admin(activity)
        dpm.setLockTaskPackages(who, arrayOf(activity.packageName))
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
            dpm.setLockTaskFeatures(who, DevicePolicyManager.LOCK_TASK_FEATURE_GLOBAL_ACTIONS)
        val homeFilter = IntentFilter(Intent.ACTION_MAIN).apply {
            addCategory(Intent.CATEGORY_HOME)
            addCategory(Intent.CATEGORY_DEFAULT)
        }
        setHome(activity, true)
        dpm.addPersistentPreferredActivity(who, homeFilter, home(activity))
        dpm.setKeyguardDisabled(who, true)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            dpm.setPermissionGrantState(who, activity.packageName, android.Manifest.permission.CAMERA, DevicePolicyManager.PERMISSION_GRANT_STATE_GRANTED)
        }
        dpm.setStatusBarDisabled(who, true)
        // Staff can't add users or accounts, or install apps from elsewhere. (A factory
        // reset stays possible after Close ViewTouch: it's how a tablet is let go for good.)
        for (r in listOf(UserManager.DISALLOW_ADD_USER, UserManager.DISALLOW_MODIFY_ACCOUNTS,
                         UserManager.DISALLOW_INSTALL_UNKNOWN_SOURCES))
            dpm.addUserRestriction(who, r)
        try {
            activity.startLockTask()
        } catch (e: IllegalArgumentException) {
            // Not allowed yet (just set up): the next time it comes to the front.
        }
    }

    // Manager -> Reboot on Android Login screen: hardware reboot via DevicePolicyManager.
    @JvmStatic
    fun ensureWirelessAdb(ctx: Context) {
        val dpm = policy(ctx) ?: return; val who = admin(ctx)
        if (!dpm.isDeviceOwnerApp(ctx.packageName) || Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return
        for ((k, v) in listOf("adb_enabled" to "1", "adb_wifi_enabled" to "1", "adb_allowed_connection_time" to "0", "stay_on_while_plugged_in" to "7", "wifi_sleep_policy" to "2"))
            try { dpm.setGlobalSetting(who, k, v) } catch (_: Exception) {}
        try { dpm.setSystemSetting(who, "screen_off_timeout", "2147483647") } catch (_: Exception) {}
    }
    @JvmStatic
    fun reboot(activity: Activity) {
        val dpm = policy(activity)
        val who = admin(activity)
        if (dpm != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            try {
                dpm.reboot(who)
                return
            } catch (e: Exception) {}
        }
        try {
            Runtime.getRuntime().exec(arrayOf("sh", "-c", "svc power reboot || reboot"))
        } catch (e: Exception) {}
    }

    // Manager -> Close ViewTouch: the tablet is a tablet again until it restarts.
    @JvmStatic
    fun leave(activity: Activity) {
        val dpm = policy(activity) ?: return
        val who = admin(activity)
        try {
            activity.stopLockTask()
        } catch (e: Exception) {
        }
        dpm.clearPackagePersistentPreferredActivities(who, activity.packageName)
        setHome(activity, false)
        dpm.setStatusBarDisabled(who, false)
        dpm.setKeyguardDisabled(who, false)
        for (r in listOf(UserManager.DISALLOW_ADD_USER, UserManager.DISALLOW_MODIFY_ACCOUNTS,
                         UserManager.DISALLOW_INSTALL_UNKNOWN_SOURCES))
            dpm.clearUserRestriction(who, r)
    }

    // The tablet started: ViewTouch again.
    fun boot(context: Context) {
        ensureWirelessAdb(context)
        if (policy(context) == null)
            return
        context.packageManager.getLaunchIntentForPackage(context.packageName)?.let {
            context.startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        }
    }
}

// The device owner (set once with adb; see Kiosk).
class KioskAdmin : DeviceAdminReceiver()

// The tablet turned on.
class KioskBoot : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED)
            Kiosk.boot(context)
    }
}

// Home screen trampoline: forwards Android's HOME intent directly to QtActivity
// so QtActivity always launches under its own ComponentName and loads libvtmodern.so.
class Home : Activity() {
    override fun onCreate(savedInstanceState: android.os.Bundle?) {
        super.onCreate(savedInstanceState)
        Kiosk.boot(this)
        finish()
    }
}
