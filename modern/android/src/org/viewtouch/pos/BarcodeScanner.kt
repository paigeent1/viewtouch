package org.viewtouch.pos

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.ImageFormat
import android.graphics.SurfaceTexture
import android.hardware.camera2.*
import android.media.AudioManager
import android.media.ImageReader
import android.media.ToneGenerator
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.view.Gravity
import android.view.Surface
import android.view.TextureView
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import java.util.concurrent.atomic.AtomicBoolean

object BarcodeScanner {
    @JvmStatic
    external fun nativeScanned(code: String)

    private var overlay: FrameLayout? = null
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var imageReader: ImageReader? = null
    private var bgThread: HandlerThread? = null
    private var bgHandler: Handler? = null
    private var useFrontCamera: Boolean = false
    private val busy = AtomicBoolean(false)
    private val finished = AtomicBoolean(false)
    private val mainHandler = Handler(Looper.getMainLooper())
    private var timeoutRunnable: Runnable? = null

    private val scanner by lazy {
        val opts = BarcodeScannerOptions.Builder()
            .setBarcodeFormats(
                Barcode.FORMAT_UPC_A,
                Barcode.FORMAT_UPC_E,
                Barcode.FORMAT_EAN_13,
                Barcode.FORMAT_EAN_8,
                Barcode.FORMAT_CODE_128,
                Barcode.FORMAT_CODE_39
            )
            .build()
        BarcodeScanning.getClient(opts)
    }

    @JvmStatic
    fun start(activity: Activity) {
        mainHandler.post {
            if (overlay != null) {
                stop(activity)
                return@post
            }
            if (activity.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                activity.requestPermissions(arrayOf(Manifest.permission.CAMERA), 901)
                return@post
            }
            openOverlay(activity)
        }
    }

    private fun openOverlay(activity: Activity) {
        finished.set(false)
        busy.set(false)
        val root = activity.window.decorView as ViewGroup

        val container = FrameLayout(activity).apply {
            setBackgroundColor(Color.parseColor("#CC0A0D12"))
            isClickable = true
            isFocusable = true
        }

        val card = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.parseColor("#181C24"))
            setPadding(24, 20, 24, 20)
            val lp = FrameLayout.LayoutParams(400, 520, Gravity.TOP or Gravity.END)
            layoutParams = lp
        }

        val header = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }

        val title = TextView(activity).apply {
            text = "Scan UPC Barcode"
            textSize = 20f
            setTextColor(Color.parseColor("#E6B800"))
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }

        val flipBtn = Button(activity).apply {
            text = "Flip Camera"
            setBackgroundColor(Color.parseColor("#2B3340"))
            setTextColor(Color.WHITE)
            setOnClickListener {
                useFrontCamera = !useFrontCamera
                stop(activity)
                openOverlay(activity)
            }
        }

        val closeBtn = Button(activity).apply {
            text = "Cancel"
            setBackgroundColor(Color.parseColor("#8A2C2C"))
            setTextColor(Color.WHITE)
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            lp.leftMargin = 16
            layoutParams = lp
            setOnClickListener { stop(activity) }
        }

        header.addView(title)
        header.addView(flipBtn)
        header.addView(closeBtn)

        val previewBox = FrameLayout(activity).apply {
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
            lp.topMargin = 14
            layoutParams = lp
        }

        val textureView = TextureView(activity)
        previewBox.addView(textureView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        // Red laser guide line across the center
        val guideLine = FrameLayout(activity).apply {
            setBackgroundColor(Color.parseColor("#AAFF3333"))
            layoutParams = FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 4, Gravity.TOP or Gravity.END)
        }
        previewBox.addView(guideLine)

        card.addView(header)
        card.addView(previewBox)
        container.addView(card)
        root.addView(container, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        overlay = container

        startBackgroundThread()
        textureView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(st: SurfaceTexture, w: Int, h: Int) {
                openCamera(activity, st)
            }
            override fun onSurfaceTextureSizeChanged(st: SurfaceTexture, w: Int, h: Int) {}
            override fun onSurfaceTextureDestroyed(st: SurfaceTexture): Boolean = true
            override fun onSurfaceTextureUpdated(st: SurfaceTexture) {}
        }

        // Auto-shutoff after 20 seconds if no barcode is scanned
        val r = Runnable { stop(activity) }
        timeoutRunnable = r
        mainHandler.postDelayed(r, 10000)
    }

    @SuppressLint("MissingPermission")
    private fun openCamera(activity: Activity, surfaceTexture: SurfaceTexture) {
        try {
            val mgr = activity.getSystemService(Context.CAMERA_SERVICE) as CameraManager
            var chosenId: String? = null
            val targetFacing = if (useFrontCamera) CameraCharacteristics.LENS_FACING_FRONT else CameraCharacteristics.LENS_FACING_BACK
            for (id in mgr.cameraIdList) {
                val chars = mgr.getCameraCharacteristics(id)
                if (chars.get(CameraCharacteristics.LENS_FACING) == targetFacing) {
                    chosenId = id
                    break
                }
            }
            if (chosenId == null && mgr.cameraIdList.isNotEmpty()) {
                chosenId = mgr.cameraIdList[0]
            }
            val camId = chosenId ?: return

            surfaceTexture.setDefaultBufferSize(1280, 720)
            val previewSurface = Surface(surfaceTexture)
            imageReader = ImageReader.newInstance(1280, 720, ImageFormat.YUV_420_888, 2).apply {
                setOnImageAvailableListener({ reader ->
                    val img = reader.acquireLatestImage() ?: return@setOnImageAvailableListener
                    if (finished.get() || !busy.compareAndSet(false, true)) {
                        img.close()
                        return@setOnImageAvailableListener
                    }
                    val mediaImage = img
                    val input = InputImage.fromMediaImage(mediaImage, 0)
                    scanner.process(input)
                        .addOnSuccessListener { barcodes ->
                            for (b in barcodes) {
                                val raw = b.rawValue?.trim() ?: continue
                                if (raw.length >= 4 && finished.compareAndSet(false, true)) {
                                    try {
                                        ToneGenerator(AudioManager.STREAM_NOTIFICATION, 90)
                                            .startTone(ToneGenerator.TONE_PROP_BEEP, 120)
                                    } catch (_: Exception) {}
                                    mainHandler.post {
                                        stop(activity)
                                        nativeScanned(raw)
                                    }
                                    break
                                }
                            }
                        }
                        .addOnCompleteListener {
                            img.close()
                            busy.set(false)
                        }
                }, bgHandler)
            }

            mgr.openCamera(camId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraDevice = camera
                    val readerSurface = imageReader?.surface ?: return
                    val req = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                        addTarget(previewSurface)
                        addTarget(readerSurface)
                        set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                    }
                    @Suppress("DEPRECATION")
                    camera.createCaptureSession(listOf(previewSurface, readerSurface), object : CameraCaptureSession.StateCallback() {
                        override fun onConfigured(session: CameraCaptureSession) {
                            captureSession = session
                            try {
                                session.setRepeatingRequest(req.build(), null, bgHandler)
                            } catch (_: Exception) {}
                        }
                        override fun onConfigureFailed(session: CameraCaptureSession) {}
                    }, bgHandler)
                }
                override fun onDisconnected(camera: CameraDevice) { camera.close() }
                override fun onError(camera: CameraDevice, error: Int) { camera.close() }
            }, bgHandler)
        } catch (_: Exception) {
            stop(activity)
        }
    }

    @JvmStatic
    fun stop(activity: Activity) {
        timeoutRunnable?.let { mainHandler.removeCallbacks(it) }
        timeoutRunnable = null
        try { captureSession?.close() } catch (_: Exception) {}
        captureSession = null
        try { cameraDevice?.close() } catch (_: Exception) {}
        cameraDevice = null
        try { imageReader?.close() } catch (_: Exception) {}
        imageReader = null
        stopBackgroundThread()
        overlay?.let { view ->
            (activity.window.decorView as? ViewGroup)?.removeView(view)
        }
        overlay = null
    }

    private fun startBackgroundThread() {
        if (bgThread != null) return
        bgThread = HandlerThread("VTBarcodeCamera").also {
            it.start()
            bgHandler = Handler(it.looper)
        }
    }

    private fun stopBackgroundThread() {
        bgThread?.quitSafely()
        bgThread = null
        bgHandler = null
    }
}
