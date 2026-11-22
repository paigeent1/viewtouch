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
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.UserManager
import android.provider.Settings

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

    // Hardware Configuration -> Bluetooth: Android's Bluetooth screen (pair a scanner) without leaving
    // the kiosk: Settings may run in the pinned task until ViewTouch comes back to the front (apply()
    // sets the pinned apps back to ViewTouch alone). Back returns to ViewTouch.
    @JvmStatic
    fun openBluetooth(activity: Activity) {
        policy(activity)?.let { dpm ->
            try {
                dpm.setLockTaskPackages(admin(activity),
                    arrayOf(activity.packageName, "com.android.settings", "com.android.bluetooth"))
            } catch (e: Exception) {
            }
        }
        try {
            activity.startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
        } catch (e: Exception) {
        }
    }

    // Manager -> Reboot on Android Login screen: hardware reboot via DevicePolicyManager.
    // Wi-Fi kept at full power while ViewTouch runs: Android's power saving otherwise turns the
    // radio down when the screen is off, and the tablet drops off the network (Bar 3, 2026-11-03).
    private var wifiLock: WifiManager.WifiLock? = null
    @JvmStatic
    fun keepWifiAwake(ctx: Context) {
        if (wifiLock?.isHeld == true) return
        try {
            val wm = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            @Suppress("DEPRECATION")
            val lock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "ViewTouch")
            lock.setReferenceCounted(false)
            lock.acquire()
            wifiLock = lock
        } catch (_: Exception) {}
    }

    @JvmStatic
    fun ensureWirelessAdb(ctx: Context) {
        keepWifiAwake(ctx)
        // Wireless debugging back on after every restart, so the Pi can reach the tablet again
        // (vt-adb-ensure then switches it to port 5555). Android lets a device owner change only a
        // few global settings, and these aren't among them: this needs WRITE_SECURE_SETTINGS, granted
        // once per tablet over USB: adb shell pm grant org.viewtouch.pos android.permission.WRITE_SECURE_SETTINGS
        if (ctx.checkSelfPermission("android.permission.WRITE_SECURE_SETTINGS") == PackageManager.PERMISSION_GRANTED) {
            val cr = ctx.contentResolver
            for ((k, v) in listOf("adb_enabled" to "1", "adb_wifi_enabled" to "1", "adb_allowed_connection_time" to "0"))
                try { android.provider.Settings.Global.putString(cr, k, v) } catch (_: Exception) {}
        }
        val dpm = policy(ctx) ?: return; val who = admin(ctx)
        if (!dpm.isDeviceOwnerApp(ctx.packageName) || Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return
        for ((k, v) in listOf("adb_enabled" to "1", "adb_wifi_enabled" to "1", "adb_allowed_connection_time" to "0", "stay_on_while_plugged_in" to "7", "wifi_sleep_policy" to "2"))
            try { dpm.setGlobalSetting(who, k, v) } catch (_: Exception) {}
        try { dpm.setSystemSetting(who, "screen_off_timeout", "2147483647") } catch (_: Exception) {}
    }
    // Tablet care (net/tablet_care.hh): what the store's Network page and the Pi's scripts are told.
    // HCA 2026-11-22: port 5555 never came back after `adb reboot` and nobody could see why.
    private var lastEnsure = 0L
    private var watchingNetwork = false
    private fun prop(key: String): String = try {
        Class.forName("android.os.SystemProperties").getMethod("get", String::class.java).invoke(null, key) as? String ?: ""
    } catch (_: Throwable) { "" }

    // {grant, adb, wifiAdb, port, tlsPort, tcpPort}: whether ViewTouch holds WRITE_SECURE_SETTINGS (granted
    // once over USB), USB and Wireless debugging on, and the port the Pi can reach (5555 once it ran
    // `adb tcpip 5555`, else Wireless debugging's own). Off: switched on again (when connecting to the
    // store, else at most every 5 minutes: Android turns it off on Wi-Fi it doesn't trust yet).
    @JvmStatic
    fun debuggingState(ctx: Context, connecting: Boolean): String {
        watchNetwork(ctx)
        val cr = ctx.contentResolver
        fun global(k: String) = try { Settings.Global.getInt(cr, k, 0) } catch (_: Exception) { 0 }
        val now = android.os.SystemClock.elapsedRealtime()
        if ((connecting || global("adb_enabled") != 1 || global("adb_wifi_enabled") != 1)
            && (connecting || lastEnsure == 0L || now - lastEnsure > 5 * 60_000L)) {
            lastEnsure = now
            ensureWirelessAdb(ctx)
        }
        val tls = prop("service.adb.tls.port").toIntOrNull() ?: 0
        val tcp = (prop("service.adb.tcp.port").toIntOrNull() ?: 0).let { if (it > 0) it else prop("persist.adb.tcp.port").toIntOrNull() ?: 0 }
        return org.json.JSONObject()
            .put("grant", ctx.checkSelfPermission("android.permission.WRITE_SECURE_SETTINGS") == PackageManager.PERMISSION_GRANTED)
            .put("adb", global("adb_enabled") == 1)
            .put("wifiAdb", global("adb_wifi_enabled") == 1)
            .put("port", if (tcp > 0) tcp else tls)
            .put("tlsPort", tls)
            .put("tcpPort", tcp)
            .toString()
    }

    // Wi-Fi back (after a drop, or later than the app started): debugging on again at once.
    private fun watchNetwork(ctx: Context) {
        if (watchingNetwork || Build.VERSION.SDK_INT < Build.VERSION_CODES.N) return
        watchingNetwork = true
        try {
            val cm = ctx.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            cm.registerDefaultNetworkCallback(object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: android.net.Network) {
                    lastEnsure = android.os.SystemClock.elapsedRealtime()
                    ensureWirelessAdb(ctx.applicationContext)
                }
            })
        } catch (_: Exception) {}
    }

    // {battery, charging, rssi, uptime (s), lastRestart}.
    @JvmStatic
    fun health(ctx: Context): String {
        val o = org.json.JSONObject()
        try {
            val i = ctx.applicationContext.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val lvl = i?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
            val scl = i?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
            if (lvl >= 0 && scl > 0) o.put("battery", (lvl * 100) / scl)
            val st = i?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) ?: -1
            o.put("charging", st == BatteryManager.BATTERY_STATUS_CHARGING || st == BatteryManager.BATTERY_STATUS_FULL
                || (i?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0) != 0)
        } catch (_: Exception) {}
        try {
            val wm = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            @Suppress("DEPRECATION")
            val rssi = wm.connectionInfo?.rssi ?: -127
            if (rssi > -127 && rssi < 0) o.put("rssi", rssi)
        } catch (_: Exception) {}
        o.put("uptime", android.os.SystemClock.elapsedRealtime() / 1000)
        val why = prefs(ctx).getString("lastRestart", "") ?: ""
        if (why.isNotEmpty()) o.put("lastRestart", why)
        return o.toString()
    }

    private fun prefs(ctx: Context) = ctx.getSharedPreferences("tablet-care", Context.MODE_PRIVATE)

    @JvmStatic
    fun restarts(ctx: Context): Int = prefs(ctx).getInt("restarts", 0)

    // No store for 10 minutes and nobody signed in: ViewTouch starts again (counted). As the device owner
    // (and the home screen) it may open itself from the background.
    @JvmStatic
    fun restartApp(ctx: Context, why: String) {
        val stamp = java.text.SimpleDateFormat("MM-dd HH:mm", java.util.Locale.US).format(java.util.Date())
        prefs(ctx).edit().putInt("restarts", restarts(ctx) + 1).putString("lastRestart", "$stamp $why").commit()
        try {
            val launch = ctx.packageManager.getLaunchIntentForPackage(ctx.packageName)
            if (launch != null) {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                val pi = android.app.PendingIntent.getActivity(ctx, 7719, launch,
                    android.app.PendingIntent.FLAG_CANCEL_CURRENT or android.app.PendingIntent.FLAG_IMMUTABLE)
                val am = ctx.getSystemService(Context.ALARM_SERVICE) as android.app.AlarmManager
                am.set(android.app.AlarmManager.RTC, System.currentTimeMillis() + 1500, pi)
            }
        } catch (_: Exception) {}
        android.os.Process.killProcess(android.os.Process.myPid())
    }

    @JvmStatic
    fun batteryInfo(activity: Activity): Int {
        return try {
            val i = activity.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val lvl = i?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
            val scl = i?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
            val bm = activity.getSystemService(Context.BATTERY_SERVICE) as? BatteryManager
            val pct = if (lvl >= 0 && scl > 0) (lvl * 100) / scl else (bm?.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY) ?: 0)
            val st = i?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) ?: -1
            val chg = st == BatteryManager.BATTERY_STATUS_CHARGING || st == BatteryManager.BATTERY_STATUS_FULL || (i?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0) != 0
            pct.coerceIn(0, 100) + (if (chg) 1000 else 0)
        } catch (_: Exception) { 0 }
    }
    @JvmStatic
    fun wifiInfo(activity: Activity): Int {
        keepWifiAwake(activity)
        return try {
            val cm = activity.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            val caps = cm?.getNetworkCapabilities(cm.activeNetwork)
            val sig = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && caps != null) caps.signalStrength else Int.MIN_VALUE
            val wm = activity.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            val rssi = if (sig > -120 && sig < 0) sig else (wm?.connectionInfo?.rssi ?: -127)
            if (rssi <= -100) (if (caps?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true) 85 else 0)
            else if (rssi >= -50) 100 else (rssi + 100) * 2
        } catch (_: Exception) { 85 }
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
        // Not a locked tablet: a billboard TV still starts ViewTouch when it turns on (Billboard.kt).
        val dpm = policy(context) ?: return Billboard.boot(context)
        // ViewTouch as the home screen again (Close ViewTouch turned that off): Android may not let a
        // starting tablet open an app from here, but it always opens the home screen.
        try {
            setHome(context, true)
            dpm.addPersistentPreferredActivity(admin(context), IntentFilter(Intent.ACTION_MAIN).apply {
                addCategory(Intent.CATEGORY_HOME)
                addCategory(Intent.CATEGORY_DEFAULT)
            }, home(context))
        } catch (e: Exception) {
        }
        context.packageManager.getLaunchIntentForPackage(context.packageName)?.let {
            context.startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        }
    }
}

// The device owner (set once with adb; see Kiosk).
class KioskAdmin : DeviceAdminReceiver()

// The tablet (or a billboard TV) turned on, or ViewTouch was just updated (open the new one). Some devices say so only with QUICKBOOT_POWERON (a fast
// boot): that opens a billboard only, so a tablet does just what BOOT_COMPLETED does.
class KioskBoot : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            Intent.ACTION_BOOT_COMPLETED, Intent.ACTION_MY_PACKAGE_REPLACED -> Kiosk.boot(context)
            "android.intent.action.QUICKBOOT_POWERON", "com.htc.intent.action.QUICKBOOT_POWERON" -> Billboard.boot(context)
        }
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
