package com.anyplay.overlay

import android.app.Activity
import android.app.Dialog
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.content.res.ColorStateList
import android.util.TypedValue
import com.anyplay.engine.JsBridge
import kotlin.math.abs

/**
 * Everything AnyPlay draws on top of the game inside the RGSS plugin:
 *  - a loading spinner (until the game draws its first frame),
 *  - a "tap the screen = ENTER" layer,
 *  - the AnyPlay gamepad / keyboard / edit-mode / toolbar overlay,
 *  - the "Close game?" dialog for the Back button.
 *
 * MainActivity only creates one of these and forwards a few calls.
 */
class AnyPlayUi(
    private val activity: Activity,
    parent: ViewGroup,
    gameKey: String
) {
    private val density = activity.resources.displayMetrics.density
    private val bridge = JsBridge()
    private val root = FrameLayout(activity)
    private val loading = FrameLayout(activity)
    private val controller: OverlayController
    private var dialog: Dialog? = null
    private var loadingGone = false

    init {
        parent.addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        // 1) tap layer (lowest). Empty-space taps fall through the gamepad to here.
        root.addView(TapLayer(), FrameLayout.LayoutParams(-1, -1))

        // 2) gamepad + keyboard + toolbar
        controller = OverlayController(activity, root, bridge, gameKey, onOpenCheats = {}, showCheats = false)

        // 3) loading spinner (top-most, hides the black screen)
        loading.setBackgroundColor(Color.BLACK)
        loading.isClickable = true
        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
        }
        box.addView(ProgressBar(activity).apply {
            isIndeterminate = true
            indeterminateTintList = ColorStateList.valueOf(Color.WHITE)
        }, LinearLayout.LayoutParams((56 * density).toInt(), (56 * density).toInt()))
        box.addView(TextView(activity).apply {
            text = "Loading game…"
            setTextColor(0xCCFFFFFF.toInt())
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            gravity = Gravity.CENTER
            setPadding(0, (14 * density).toInt(), 0, 0)
        })
        loading.addView(box, FrameLayout.LayoutParams(-2, -2, Gravity.CENTER))
        root.addView(loading, FrameLayout.LayoutParams(-1, -1))
    }

    /** Called (from any thread) when the game has drawn its first frame. */
    fun onFirstFrame() {
        activity.runOnUiThread {
            if (loadingGone) return@runOnUiThread
            loadingGone = true
            loading.animate().alpha(0f).setDuration(250).withEndAction {
                root.removeView(loading)
            }.start()
        }
    }

    /** Back button. Returns true when handled (closes edit mode, or asks "Close game?"). */
    fun onBack(): Boolean {
        if (controller.handleBack()) return true
        showCloseDialog()
        return true
    }

    fun onPause() {
        controller.releaseAllKeys()
    }

    fun onDestroy() {
        dialog?.dismiss()
        controller.shutdown()
    }

    // ---- "Close game?" dialog, same look as the AnyPlay app ---------------

    private fun showCloseDialog() {
        if (dialog?.isShowing == true) return
        controller.releaseAllKeys()

        fun round(color: Int, dp: Float) = GradientDrawable().apply {
            setColor(color); cornerRadius = dp * density
        }

        val content = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            background = round(0xFF1F2129.toInt(), 22f)
            val p = (24 * density).toInt()
            setPadding(p, p, p, (20 * density).toInt())
        }
        content.addView(TextView(activity).apply {
            text = "Close game?"
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 22f)
            typeface = android.graphics.Typeface.DEFAULT_BOLD
        })
        content.addView(TextView(activity).apply {
            text = "Do you want to close the game and go back? Unsaved progress may be lost."
            setTextColor(0xFFB8BCC8.toInt())
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            setPadding(0, (12 * density).toInt(), 0, (24 * density).toInt())
        })

        val d = Dialog(activity)
        d.requestWindowFeature(Window.FEATURE_NO_TITLE)

        fun button(label: String, bg: Int, onClick: () -> Unit) = TextView(activity).apply {
            text = label
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 17f)
            typeface = android.graphics.Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            background = round(bg, 10f)
            setPadding((26 * density).toInt(), (14 * density).toInt(), (26 * density).toInt(), (14 * density).toInt())
            setOnClickListener { onClick() }
        }

        val row = LinearLayout(activity).apply { gravity = Gravity.END }
        row.addView(button("No", 0xFF3A3D48.toInt()) { d.dismiss() })
        row.addView(button("Yes", 0xFFE04B4B.toInt()) {
            d.dismiss()
            activity.finish() // MainActivity.onDestroy() ends the process cleanly
        }, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = (14 * density).toInt() })
        content.addView(row)

        d.setContentView(content)
        d.window?.apply {
            setBackgroundDrawableResource(android.R.color.transparent)
            setLayout((520 * density).toInt().coerceAtMost((activity.resources.displayMetrics.widthPixels * 0.9f).toInt()),
                WindowManager.LayoutParams.WRAP_CONTENT)
            // keep the game's full-screen mode while the dialog is up
            setFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        }
        dialog = d
        d.show()
        d.window?.apply {
            decorView.systemUiVisibility = activity.window.decorView.systemUiVisibility
            clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        }
    }

    // ---- Tap anywhere on empty screen = press ENTER ------------------------

    private inner class TapLayer : View(activity) {
        private var downX = 0f
        private var downY = 0f
        private var downTime = 0L
        private val slop = ViewConfiguration.get(activity).scaledTouchSlop

        init {
            isFocusable = false
        }

        override fun onTouchEvent(ev: MotionEvent): Boolean {
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downX = ev.x; downY = ev.y; downTime = ev.eventTime
                }
                MotionEvent.ACTION_UP -> {
                    if (abs(ev.x - downX) < slop * 2 && abs(ev.y - downY) < slop * 2 &&
                        ev.eventTime - downTime < 350
                    ) {
                        bridge.tap(KeyEvent.KEYCODE_ENTER)
                    }
                }
            }
            return true
        }
    }
}
