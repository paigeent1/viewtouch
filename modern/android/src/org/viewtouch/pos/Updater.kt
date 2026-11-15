package org.viewtouch.pos

import android.app.PendingIntent
import android.app.admin.DevicePolicyManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import androidx.core.content.FileProvider
import java.io.File

// An update the store sent (net/remote_session.cpp saved it in the cache).
// - A tablet set up for ViewTouch (the device owner, Kiosk.kt) installs it by itself, asking no one:
//   ViewTouch closes, Android puts the new one in and opens it again (KioskBoot: MY_PACKAGE_REPLACED).
//   The screen only starts this while nobody is signed in on it (Main.qml), so no order is cut off.
// - Anywhere else Android's installer asks whoever is here. The first time, Android asks to allow
//   ViewTouch to install apps; then Update again.
object Updater {
    private const val AUTHORITY = "org.viewtouch.pos.updates"
    private const val TAG = "ViewTouch"

    // Installs without asking anyone: only the device owner may.
    @JvmStatic
    fun silent(context: Context): Boolean = try {
        (context.getSystemService(Context.DEVICE_POLICY_SERVICE) as DevicePolicyManager)
            .isDeviceOwnerApp(context.packageName)
    } catch (_: Exception) { false }

    @JvmStatic
    fun install(context: Context, path: String) {
        if (silent(context)) {
            // Writing 100+ MB into the installer: not on the screen's thread.
            Thread { if (!installQuietly(context.applicationContext, path)) Handler(Looper.getMainLooper()).post { ask(context, path) } }.start()
            return
        }
        Handler(Looper.getMainLooper()).post { ask(context, path) }
    }

    private fun installQuietly(context: Context, path: String): Boolean {
        val file = File(path)
        return try {
            val installer = context.packageManager.packageInstaller
            val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
            params.setAppPackageName(context.packageName)
            params.setSize(file.length())
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED)
            val id = installer.createSession(params)
            installer.openSession(id).use { session ->
                file.inputStream().use { input ->
                    session.openWrite("viewtouch.apk", 0, file.length()).use { out ->
                        input.copyTo(out, 1 shl 20)
                        session.fsync(out)
                    }
                }
                // The installer fills in how it went: the intent must be mutable.
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                    (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
                val done = PendingIntent.getBroadcast(context, id, Intent(context, UpdateInstalled::class.java), flags)
                session.commit(done.intentSender)
            }
            Log.i(TAG, "update: installing ${file.name} by itself")
            true
        } catch (e: Exception) {
            Log.w(TAG, "update: couldn't install ${file.name} by itself", e)
            false
        }
    }

    // Android's installer, asking whoever is here.
    private fun ask(context: Context, path: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !context.packageManager.canRequestPackageInstalls()) {
            context.startActivity(
                Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + context.packageName))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
            return
        }
        val uri = FileProvider.getUriForFile(context, AUTHORITY, File(path))
        context.startActivity(
            Intent(Intent.ACTION_VIEW)
                .setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        )
    }
}

// How a quiet install went. Done: ViewTouch is already closed (KioskBoot opens the new one).
// Android wanting someone to confirm after all: its screen, for whoever is here.
class UpdateInstalled : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            @Suppress("DEPRECATION")
            val confirm = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
            try { confirm?.let { context.startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } } catch (_: Exception) {}
            return
        }
        if (status != PackageInstaller.STATUS_SUCCESS)
            Log.w("ViewTouch", "update: install failed ($status): ${intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)}")
    }
}
