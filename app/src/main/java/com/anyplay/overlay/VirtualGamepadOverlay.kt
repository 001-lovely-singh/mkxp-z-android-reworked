package com.anyplay.overlay

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.view.MotionEvent
import android.view.ViewConfiguration
import android.widget.FrameLayout
import com.anyplay.engine.JsBridge
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * VirtualGamepadOverlay
 * -----------------------
 * Full-screen, transparent layer that hosts every control of a
 * [GamepadLayout] (the D-pad and the buttons). Each control is a
 * [ControlView]; this class positions them from the layout's fractional
 * coordinates and, in EDIT MODE, turns the whole layer into the layout
 * editor shown in the reference:
 *
 *   - a 10 dp grid is drawn over everything,
 *   - touches are intercepted (the game underneath gets nothing),
 *   - dragging a control moves it, snapping its top-left corner to the grid,
 *   - tapping a control (no drag) reports [onControlTapped] so the host can
 *     open the settings dialog.
 *
 * In PLAY MODE touches on empty space fall straight through to the game
 * (this view is not clickable), while each control handles its own touches
 * — multi-touch (hold a direction + tap a button) works because every
 * control is its own View.
 *
 * What this class deliberately does NOT do: decide which buttons exist
 * (the layout does), show/hide itself (OverlayController does), or persist
 * anything (GamepadLayoutStore does).
 */
class VirtualGamepadOverlay(
    context: Context,
    private val jsBridge: JsBridge,
    initialLayout: GamepadLayout
) : FrameLayout(context) {

    /** Fired when a control is tapped (not dragged) in edit mode. */
    var onControlTapped: ((id: String) -> Unit)? = null

    /** Fired once when a drag finishes in edit mode (one undo step). */
    var onLayoutEdited: (() -> Unit)? = null

    var layout: GamepadLayout = initialLayout
        set(value) {
            field = value
            syncViews()
        }

    var editMode: Boolean = false
        set(value) {
            if (field == value) return
            field = value
            releaseAllHeldKeys()
            dragTarget = null
            dragging = false
            invalidate()
        }

    private val density = resources.displayMetrics.density
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop
    private val gridStep = 10f * density

    private val sink = object : KeySink {
        override fun keyDown(code: Int) {
            jsBridge.sendKeyDown(code)
        }

        override fun keyUp(code: Int) {
            jsBridge.sendKeyUp(code)
        }
    }

    private val views = LinkedHashMap<String, ControlView>()

    private val gridPaint = Paint().apply {
        color = 0x16FFFFFF
        strokeWidth = 0.8f * density
        style = Paint.Style.STROKE
    }

    // Edit-mode drag bookkeeping.
    private var dragTarget: ControlView? = null
    private var dragging = false
    private var downX = 0f
    private var downY = 0f
    private var startLeft = 0f
    private var startTop = 0f

    init {
        setWillNotDraw(false)
        syncViews()
    }

    // ---- Keeping child views in sync with the layout ---------------------

    private fun syncViews() {
        val ids = layout.controls.map { it.id }.toSet()
        for (id in views.keys.filter { it !in ids }) {
            views.remove(id)?.let {
                it.releaseIfHeld()
                removeView(it)
            }
        }
        for (spec in layout.controls) {
            val existing = views[spec.id]
            if (existing == null) {
                val created = ControlView(context, spec, sink)
                views[spec.id] = created
                addView(created, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
            } else if (existing.spec != spec) {
                existing.spec = spec
            }
        }
        requestLayout()
    }

    /** Convenience for the edit dialog's live preview: swap one control's spec. */
    fun updateSpec(spec: ControlSpec) {
        layout = layout.replace(spec)
    }

    // ---- Measure / layout: exact dp sizes, fractional positions ----------

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(
            MeasureSpec.getSize(widthMeasureSpec),
            MeasureSpec.getSize(heightMeasureSpec)
        )
        for (i in 0 until childCount) getChildAt(i).measure(0, 0)
    }

    override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
        val w = r - l
        val h = b - t
        for (i in 0 until childCount) {
            val child = getChildAt(i) as? ControlView ?: continue
            val cw = child.measuredWidth
            val ch = child.measuredHeight
            val left = (child.spec.cx * w - cw / 2f).roundToInt().coerceIn(0, (w - cw).coerceAtLeast(0))
            val top = (child.spec.cy * h - ch / 2f).roundToInt().coerceIn(0, (h - ch).coerceAtLeast(0))
            child.layout(left, top, left + cw, top + ch)
        }
    }

    // ---- Edit mode: grid + drag -------------------------------------------

    override fun onDraw(canvas: Canvas) {
        if (!editMode) return
        var x = 0f
        while (x <= width) {
            canvas.drawLine(x, 0f, x, height.toFloat(), gridPaint)
            x += gridStep
        }
        var y = 0f
        while (y <= height) {
            canvas.drawLine(0f, y, width.toFloat(), y, gridPaint)
            y += gridStep
        }
    }

    override fun onInterceptTouchEvent(ev: MotionEvent): Boolean =
        editMode || super.onInterceptTouchEvent(ev)

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        if (!editMode) return super.onTouchEvent(ev)

        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                dragTarget = hitTest(ev.x, ev.y)
                dragging = false
                dragTarget?.let {
                    downX = ev.x
                    downY = ev.y
                    startLeft = it.left.toFloat()
                    startTop = it.top.toFloat()
                }
            }

            MotionEvent.ACTION_MOVE -> {
                val target = dragTarget
                if (target != null) {
                    val dx = ev.x - downX
                    val dy = ev.y - downY
                    if (!dragging && (abs(dx) > touchSlop || abs(dy) > touchSlop)) dragging = true
                    if (dragging) moveControl(target, startLeft + dx, startTop + dy)
                }
            }

            MotionEvent.ACTION_UP -> {
                val target = dragTarget
                if (target != null) {
                    if (dragging) onLayoutEdited?.invoke() else onControlTapped?.invoke(target.spec.id)
                }
                dragTarget = null
                dragging = false
            }

            MotionEvent.ACTION_CANCEL -> {
                if (dragTarget != null && dragging) onLayoutEdited?.invoke()
                dragTarget = null
                dragging = false
            }
        }
        return true
    }

    private fun hitTest(x: Float, y: Float): ControlView? {
        for (i in childCount - 1 downTo 0) {
            val child = getChildAt(i) as? ControlView ?: continue
            if (x >= child.left && x < child.right && y >= child.top && y < child.bottom) return child
        }
        return null
    }

    /** Moves [view] so its top-left lands on the nearest grid point (clamped on-screen). */
    private fun moveControl(view: ControlView, desiredLeft: Float, desiredTop: Float) {
        if (width == 0 || height == 0) return
        val snappedLeft = (desiredLeft / gridStep).roundToInt() * gridStep
        val snappedTop = (desiredTop / gridStep).roundToInt() * gridStep
        val left = snappedLeft.coerceIn(0f, (width - view.width).toFloat().coerceAtLeast(0f))
        val top = snappedTop.coerceIn(0f, (height - view.height).toFloat().coerceAtLeast(0f))
        val newCx = (left + view.width / 2f) / width
        val newCy = (top + view.height / 2f) / height
        val spec = view.spec
        if (abs(spec.cx - newCx) > 1e-4f || abs(spec.cy - newCy) > 1e-4f) {
            layout = layout.replace(spec.copy(cx = newCx, cy = newCy))
        }
    }

    /**
     * Call from GamePlayActivity.onPause(), or right before hiding this
     * overlay — releases every key currently held so nothing gets stuck
     * down in the game.
     */
    fun releaseAllHeldKeys() {
        views.values.forEach { it.releaseIfHeld() }
    }
}