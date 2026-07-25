// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// Hoenn Forge: secondary Presentation hosts a full-screen overlay container so
// Pokédex UI can appear on the **bottom** physical display (Thor) without a
// second competing Presentation.

package org.citra.citra_emu.display

import android.app.Presentation
import android.content.Context
import android.graphics.Color
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.os.Build
import android.os.Bundle
import android.view.Display
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.utils.Log

class SecondaryDisplay(val context: Context) : DisplayManager.DisplayListener {
    private var pres: SecondaryDisplayPresentation? = null
    private val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
    private val vd: VirtualDisplay
    var preferredDisplayId = -1
    var currentDisplayId = -1

    val availableDisplays: List<Display>
        get() = getSecondaryDisplays()

    /** True when a real secondary (bottom) display is in use, not only HiddenDisplay. */
    val hasPhysicalSecondary: Boolean
        get() = currentDisplayId >= 0 && availableDisplays.any { it.displayId == currentDisplayId }

    init {
        vd = displayManager.createVirtualDisplay(
            "HiddenDisplay",
            1920,
            1080,
            320,
            null,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION,
        )
        displayManager.registerDisplayListener(this, null)
    }

    fun updateSurface() {
        val surface = pres?.getSurfaceHolder()?.surface
        if (surface != null && surface.isValid) {
            NativeLibrary.secondarySurfaceChanged(surface)
        } else {
            Log.warning("SecondaryDisplay Attempted to update null or invalid surface")
        }
    }

    fun destroySurface() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

    /**
     * Show a full-screen UI layer on the secondary (bottom) Presentation.
     * @return true if overlay was attached to a live presentation
     */
    fun showOverlay(view: View): Boolean {
        val p = pres ?: return false
        return try {
            p.showOverlay(view)
            true
        } catch (e: Exception) {
            Log.warning("SecondaryDisplay showOverlay failed: $e")
            false
        }
    }

    fun hideOverlay() {
        try {
            pres?.hideOverlay()
        } catch (_: Exception) {
        }
    }

    fun isOverlayVisible(): Boolean = pres?.isOverlayVisible() == true

    private fun getSecondaryDisplays(): List<Display> {
        val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        val currentDisplayId = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            context.display.displayId
        } else {
            @Suppress("DEPRECATION")
            (context.getSystemService(Context.WINDOW_SERVICE) as WindowManager)
                .defaultDisplay.displayId
        }
        val displays = dm.displays
        val presDisplays = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
        return displays.filter {
            val isPresentable = presDisplays.any { pd -> pd.displayId == it.displayId }
            val isNotDefaultOrPresentable =
                (it != null && it.displayId != Display.DEFAULT_DISPLAY) || isPresentable

            isNotDefaultOrPresentable &&
                it.displayId != currentDisplayId &&
                it.name != "HiddenDisplay" &&
                it.state != Display.STATE_OFF &&
                it.isValid
        }
    }

    fun updateDisplay() {
        if (context is android.app.Activity && (context.isFinishing || context.isDestroyed)) {
            return
        }

        val displayToUse = if (availableDisplays.isEmpty() ||
            IntSetting.SECONDARY_DISPLAY_LAYOUT.int == SecondaryDisplayLayout.NONE.int ||
            !BooleanSetting.ENABLE_SECONDARY_DISPLAY.boolean
        ) {
            currentDisplayId = -1
            vd.display
        } else if (preferredDisplayId >= 0 &&
            availableDisplays.any { it.displayId == preferredDisplayId }
        ) {
            currentDisplayId = preferredDisplayId
            availableDisplays.first { it.displayId == preferredDisplayId }
        } else {
            val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
            val default = dm.displays.first { it.displayId == Display.DEFAULT_DISPLAY }
            currentDisplayId = availableDisplays.firstOrNull {
                it.name != default.name && !it.name.contains("Built", true)
            }?.displayId
                ?: availableDisplays[0].displayId
            availableDisplays.first { it.displayId == currentDisplayId }
        }

        if (pres?.display == displayToUse) return

        releasePresentation()

        try {
            pres = SecondaryDisplayPresentation(context, displayToUse!!, this)
            pres?.show()
        } catch (_: WindowManager.BadTokenException) {
            pres = null
        } catch (_: WindowManager.InvalidDisplayException) {
            pres = null
        }
    }

    fun releasePresentation() {
        try {
            pres?.dismiss()
        } catch (_: Exception) {
        }
        pres = null
    }

    fun releaseVD() {
        displayManager.unregisterDisplayListener(this)
        vd.release()
    }

    override fun onDisplayAdded(displayId: Int) {
        updateDisplay()
    }

    override fun onDisplayRemoved(displayId: Int) {
        updateDisplay()
    }

    override fun onDisplayChanged(displayId: Int) {
        updateDisplay()
    }
}

class SecondaryDisplayPresentation(
    context: Context,
    display: Display,
    val parent: SecondaryDisplay,
) : Presentation(context, display) {
    private lateinit var root: FrameLayout
    private lateinit var surfaceView: SurfaceView
    private lateinit var overlay: FrameLayout
    private var touchscreenPointerId = -1
    private var overlayVisible = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyGameTouchFlags()

        root = FrameLayout(context)
        surfaceView = SurfaceView(context)
        overlay = FrameLayout(context).apply {
            visibility = View.GONE
            isClickable = true
            isFocusable = true
            isFocusableInTouchMode = true
            setBackgroundColor(Color.parseColor("#FF0D1B2A"))
        }

        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                Log.debug("SecondaryDisplay Surface created")
            }

            override fun surfaceChanged(
                holder: SurfaceHolder,
                format: Int,
                width: Int,
                height: Int,
            ) {
                Log.debug("SecondaryDisplay Surface changed: ${width}x$height")
                parent.updateSurface()
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                Log.debug("SecondaryDisplay Surface destroyed")
                parent.destroySurface()
            }
        })

        surfaceView.setOnTouchListener { _, event ->
            if (overlayVisible) return@setOnTouchListener false
            val pointerIndex = event.actionIndex
            val pointerId = event.getPointerId(pointerIndex)
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (touchscreenPointerId == -1) {
                        touchscreenPointerId = pointerId
                        NativeLibrary.onSecondaryTouchEvent(
                            event.getX(pointerIndex),
                            event.getY(pointerIndex),
                            true,
                        )
                    }
                }
                MotionEvent.ACTION_MOVE -> {
                    val index = event.findPointerIndex(touchscreenPointerId)
                    if (index != -1) {
                        NativeLibrary.onSecondaryTouchMoved(
                            event.getX(index),
                            event.getY(index),
                        )
                    }
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    if (pointerId == touchscreenPointerId) {
                        NativeLibrary.onSecondaryTouchEvent(0f, 0f, false)
                        touchscreenPointerId = -1
                    }
                }
            }
            true
        }

        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            ),
        )
        root.addView(
            overlay,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            ),
        )
        setContentView(root)
    }

    fun showOverlay(view: View) {
        if (view.parent != null) {
            (view.parent as? ViewGroup)?.removeView(view)
        }
        overlay.removeAllViews()
        overlay.addView(
            view,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            ),
        )
        overlay.visibility = View.VISIBLE
        overlayVisible = true
        // Allow focus/touch for Pokédex buttons
        window?.clearFlags(
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
        )
        overlay.requestFocus()
        Log.warning("SecondaryDisplay: overlay shown (${overlay.childCount} children)")
    }

    fun hideOverlay() {
        overlay.removeAllViews()
        overlay.visibility = View.GONE
        overlayVisible = false
        applyGameTouchFlags()
        Log.warning("SecondaryDisplay: overlay hidden")
    }

    fun isOverlayVisible(): Boolean = overlayVisible

    private fun applyGameTouchFlags() {
        window?.setFlags(
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
        )
    }

    fun getSurfaceHolder(): SurfaceHolder = surfaceView.holder
}
