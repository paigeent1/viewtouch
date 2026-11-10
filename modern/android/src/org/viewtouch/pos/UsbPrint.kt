package org.viewtouch.pos

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.content.ContextCompat
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

// A receipt printer plugged into the tablet's USB (through a powered USB-C hub with power
// pass-through), for print/usb_print.cpp. No printer model is known here: any device with a
// printer interface (class 7) works, else the first one with a bulk OUT endpoint.
//   devices()  what's plugged in (JSON; the C++ side picks the printer)
//   open()     Android's permission (asked on the screen the first time, waited for), the
//              printer's interface claimed
//   transfer() a chunk of the ticket, blocking: returns the bytes sent, or -1
//   close()    let go of it (after every ticket)
// Android remembers the permission while the printer stays plugged in; ticking "Always open
// ViewTouch..." in its question remembers it for good (the manifest's device_filter.xml: printers).
// The C++ side calls these from a printing thread, never the screen's.
object UsbPrint {
    private const val TAG = "ViewTouchUsb"
    private const val ACTION_PERMISSION = "org.viewtouch.pos.USB_PERMISSION"
    private const val EXTRA_NAME = "org.viewtouch.pos.USB_DEVICE_NAME"

    private class Open(val connection: UsbDeviceConnection, val iface: UsbInterface, val endpoint: UsbEndpoint)

    private val opened = ConcurrentHashMap<String, Open>()
    private val waiting = ConcurrentHashMap<String, CountDownLatch>()
    private var registered = false
    private val main = Handler(Looper.getMainLooper())

    // ViewTouchApplication: the permission answers, and printers plugged in or unplugged.
    @JvmStatic
    fun init(context: Context) {
        register(context.applicationContext)
    }

    private fun register(app: Context) {
        synchronized(this) {
            if (registered) return
            registered = true
        }
        try {
            // The answer to our question. FLAG_IMMUTABLE: Android adds nothing to the intent, so the
            // device's name is ours (EXTRA_NAME) and whether it was allowed is asked of UsbManager.
            ContextCompat.registerReceiver(app, object : BroadcastReceiver() {
                override fun onReceive(c: Context, intent: Intent) {
                    val name = intent.getStringExtra(EXTRA_NAME) ?: return
                    waiting[name]?.countDown()
                }
            }, IntentFilter(ACTION_PERMISSION), ContextCompat.RECEIVER_NOT_EXPORTED)
            val plug = IntentFilter().apply {
                addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            }
            // Sent by Android only (protected broadcasts).
            ContextCompat.registerReceiver(app, object : BroadcastReceiver() {
                override fun onReceive(c: Context, intent: Intent) {
                    val device = deviceOf(intent) ?: return
                    if (intent.action == UsbManager.ACTION_USB_DEVICE_DETACHED) {
                        // A ticket going out fails on its own (its transfer returns -1); one waiting
                        // for the permission stops waiting.
                        Log.i(TAG, "unplugged: ${describe(device)}")
                        waiting[device.deviceName]?.countDown()
                    } else {
                        Log.i(TAG, "plugged in: ${describe(device)}")
                    }
                }
            }, plug, ContextCompat.RECEIVER_EXPORTED)
        } catch (e: Exception) {
            Log.w(TAG, "can't listen for USB printers: $e")
        }
    }

    @Suppress("DEPRECATION")
    private fun deviceOf(intent: Intent): UsbDevice? {
        if (Build.VERSION.SDK_INT >= 33)
            return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        return intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
    }

    private fun manager(context: Context) = context.getSystemService(Context.USB_SERVICE) as UsbManager?

    private fun isPrinter(device: UsbDevice): Boolean {
        if (device.deviceClass == UsbConstants.USB_CLASS_PRINTER) return true
        for (i in 0 until device.interfaceCount)
            if (device.getInterface(i).interfaceClass == UsbConstants.USB_CLASS_PRINTER) return true
        return false
    }

    // The interface and bulk OUT endpoint to send to: a printer interface's, else the first.
    private fun endpointOf(device: UsbDevice): Pair<UsbInterface, UsbEndpoint>? {
        var first: Pair<UsbInterface, UsbEndpoint>? = null
        for (i in 0 until device.interfaceCount) {
            val f = device.getInterface(i)
            for (e in 0 until f.endpointCount) {
                val ep = f.getEndpoint(e)
                if (ep.type != UsbConstants.USB_ENDPOINT_XFER_BULK || ep.direction != UsbConstants.USB_DIR_OUT) continue
                if (f.interfaceClass == UsbConstants.USB_CLASS_PRINTER) return Pair(f, ep)
                if (first == null) first = Pair(f, ep)
            }
        }
        return first
    }

    private fun describe(device: UsbDevice): String {
        val words = listOfNotNull(device.manufacturerName, device.productName).joinToString(" ").trim()
        return if (words.isEmpty()) device.deviceName else words
    }

    // What's plugged in: [{name, manufacturer, product, serial, vendorId, productId, printer, bulkOut,
    // permitted}]. The serial number shows only once ViewTouch may use the device (Android 10+).
    @JvmStatic
    fun devices(context: Context): String {
        val out = JSONArray()
        val usb = manager(context) ?: return out.toString()
        for (device in usb.deviceList.values) {
            val permitted = usb.hasPermission(device)
            val serial = try {
                if (permitted || Build.VERSION.SDK_INT < 29) device.serialNumber ?: "" else ""
            } catch (e: SecurityException) {
                ""
            }
            out.put(JSONObject().apply {
                put("name", device.deviceName)
                put("manufacturer", device.manufacturerName ?: "")
                put("product", device.productName ?: "")
                put("serial", serial)
                put("vendorId", device.vendorId)
                put("productId", device.productId)
                put("printer", isPrinter(device))
                put("bulkOut", endpointOf(device) != null)
                put("permitted", permitted)
            })
        }
        return out.toString()
    }

    // Ready to print on `name`: "" when it is, else why not. Asks Android's permission when it
    // hasn't been given (the question is on the screen; waits up to `waitMs` for the answer).
    @JvmStatic
    fun open(context: Context, name: String, waitMs: Int): String {
        val app = context.applicationContext
        register(app)
        val usb = manager(app) ?: return "this tablet has no USB host support"
        var device = usb.deviceList[name] ?: return "the USB printer was unplugged"
        if (!usb.hasPermission(device)) {
            val latch = CountDownLatch(1)
            waiting[name] = latch
            main.post { ask(app, usb, device) }
            try {
                latch.await(waitMs.toLong().coerceAtLeast(1000), TimeUnit.MILLISECONDS)
            } catch (e: InterruptedException) {
            }
            waiting.remove(name)
            device = usb.deviceList[name] ?: return "the USB printer was unplugged"
            if (!usb.hasPermission(device))
                return "ViewTouch may not use the USB printer yet: tap OK on the tablet's question " +
                    "(tick \"Always\"), then Reprint"
        }
        val (iface, endpoint) = endpointOf(device) ?: return "the USB device ${describe(device)} takes no data (not a printer?)"
        val connection = usb.openDevice(device) ?: return "the tablet could not open the USB printer ${describe(device)}"
        if (!connection.claimInterface(iface, true)) {
            connection.close()
            return "the USB printer ${describe(device)} is in use by another app"
        }
        opened.put(name, Open(connection, iface, endpoint))?.let { closeOpen(it) }
        Log.i(TAG, "printing on ${describe(device)}")
        return ""
    }

    // Android's question "Allow ViewTouch to access ...?". On a tablet only for ViewTouch (Kiosk.kt)
    // the question (System UI) may show over the pinned app for now.
    private fun ask(app: Context, usb: UsbManager, device: UsbDevice) {
        try {
            Kiosk.allowSystemDialogs(app)
            val intent = Intent(ACTION_PERMISSION).setPackage(app.packageName).putExtra(EXTRA_NAME, device.deviceName)
            val pending = PendingIntent.getBroadcast(app, device.deviceName.hashCode(), intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
            usb.requestPermission(device, pending)
        } catch (e: Exception) {
            Log.w(TAG, "can't ask for the USB printer: $e")
            waiting[device.deviceName]?.countDown()
        }
    }

    // Part of the ticket (`length` bytes from `offset`), blocking up to `timeoutMs`: the bytes sent, or -1.
    @JvmStatic
    fun transfer(name: String, data: ByteArray, offset: Int, length: Int, timeoutMs: Int): Int {
        val o = opened[name] ?: return -1
        return try {
            o.connection.bulkTransfer(o.endpoint, data, offset, length, timeoutMs)
        } catch (e: Exception) {
            -1
        }
    }

    @JvmStatic
    fun close(name: String) {
        opened.remove(name)?.let { closeOpen(it) }
    }

    private fun closeOpen(o: Open) {
        try {
            o.connection.releaseInterface(o.iface)
        } catch (e: Exception) {
        }
        try {
            o.connection.close()
        } catch (e: Exception) {
        }
    }
}
