package com.anyplay.overlay

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.view.View
import android.widget.LinearLayout
import androidx.core.graphics.PathParser
import kotlin.math.hypot

internal enum class IconKind { GAMEPAD, GAMEPAD_OFF, PENCIL, STAR, KEYBOARD, CHECK, UNDO, RESET, CLOSE, ADD, APPLY_ALL }

/**
 * Vector icons for the right-hand icon column, authored on a 24x24 unit
 * grid and scaled at draw time. Plain Paths (no drawable resources), so
 * there are no XML files to add — consistent with the rest of the overlay
 * code. Glyph sizes were matched to the reference (e.g. gamepad ≈ 16 x 11
 * dp, keyboard ≈ 16 x 11 dp, pencil ≈ 15 dp, sparkle ≈ 17 dp).
 */
internal object OverlayIcons {

    private val cache = HashMap<IconKind, Path>()

    fun path(kind: IconKind): Path = cache.getOrPut(kind) { build(kind) }

    private fun parse(d: String): Path = try {
        PathParser.createPathFromPathData(d)
    } catch (e: Exception) {
        Path()
    }

    private fun build(kind: IconKind): Path = when (kind) {
        IconKind.GAMEPAD -> gamepad(withSlash = false)
        IconKind.GAMEPAD_OFF -> gamepad(withSlash = true)
        IconKind.STAR -> Path().apply {
            // 4-point sparkle with concave sides.
            moveTo(12f, 1f)
            quadTo(13.3f, 10.7f, 23f, 12f)
            quadTo(13.3f, 13.3f, 12f, 23f)
            quadTo(10.7f, 13.3f, 1f, 12f)
            quadTo(10.7f, 10.7f, 12f, 1f)
            close()
        }
        IconKind.PENCIL -> parse(
            "M3,17.25V21h3.75L17.81,9.94l-3.75,-3.75L3,17.25zM20.71,7.04c0.39,-0.39 0.39,-1.02 0,-1.41" +
                    "l-2.34,-2.34c-0.39,-0.39 -1.02,-0.39 -1.41,0l-1.83,1.83 3.75,3.75 1.83,-1.83z"
        )
        IconKind.KEYBOARD -> parse(
            "M20,5L4,5c-1.1,0 -1.99,0.9 -1.99,2L2,17c0,1.1 0.9,2 2,2h16c1.1,0 2,-0.9 2,-2L22,7" +
                    "c0,-1.1 -0.9,-2 -2,-2zM11,8h2v2h-2L11,8zM11,11h2v2h-2v-2zM8,8h2v2L8,10L8,8zM8,11h2v2L8,13v-2z" +
                    "M5,11h2v2L5,13v-2zM5,8h2v2L5,10L5,8zM8,15h8v2L8,17v-2zM16,13h-2v-2h2v2zM16,10h-2L14,8h2v2z" +
                    "M19,13h-2v-2h2v2zM19,10h-2L17,8h2v2z"
        )
        IconKind.CHECK -> parse("M9,16.17L4.83,12l-1.42,1.41L9,19 21,7l-1.41,-1.41z")
        IconKind.APPLY_ALL -> parse(
            "M16,1H4c-1.1,0 -2,0.9 -2,2v14h2V3h12V1zM19,5H8c-1.1,0 -2,0.9 -2,2v14c0,1.1 0.9,2 2,2h11c1.1,0 2,-0.9 2,-2V7" +
                    "c0,-1.1 -0.9,-2 -2,-2zM19,21H8V7h11v14z"
        )
        IconKind.ADD -> parse("M19,13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z")
        IconKind.CLOSE -> parse(
            "M19,6.41L17.59,5 12,10.59 6.41,5 5,6.41 10.59,12 5,17.59 6.41,19 12,13.41 17.59,19 19,17.59 13.41,12z"
        )
        IconKind.RESET -> parse(
            "M17.65,6.35C16.2,4.9 14.21,4 12,4c-4.42,0 -7.99,3.58 -7.99,8s3.57,8 7.99,8c3.73,0 6.84,-2.55 7.73,-6" +
                    "h-2.08c-0.82,2.33 -3.04,4 -5.65,4 -3.31,0 -6,-2.69 -6,-6s2.69,-6 6,-6c1.66,0 3.14,0.69 4.22,1.78" +
                    "L13,11h7V4l-2.35,2.35z"
        )
        IconKind.UNDO -> parse(
            "M12.5,8c-2.65,0 -5.05,0.99 -6.9,2.6L2,7v9h9l-3.62,-3.62c1.39,-1.16 3.16,-1.88 5.12,-1.88 " +
                    "3.54,0 6.55,2.31 7.6,5.5l2.37,-0.78C21.08,11.03 17.15,8 12.5,8z"
        )
    }

    /** Game-controller silhouette: rounded body + two grips, cut-out D-pad cross and two buttons. */
    private fun gamepad(withSlash: Boolean): Path {
        val body = Path().apply { addRoundRect(RectF(2f, 5.8f, 22f, 15.4f), 4.6f, 4.6f, Path.Direction.CW) }
        val grips = Path().apply {
            addCircle(6.4f, 14.4f, 4.0f, Path.Direction.CW)
            addCircle(17.6f, 14.4f, 4.0f, Path.Direction.CW)
        }
        body.op(grips, Path.Op.UNION)

        val cross = Path().apply {
            addRect(5.6f, 9.65f, 10.0f, 11.15f, Path.Direction.CW)
            addRect(7.05f, 8.2f, 8.55f, 12.6f, Path.Direction.CW)
        }
        body.op(cross, Path.Op.DIFFERENCE)

        val dots = Path().apply {
            addCircle(16.2f, 9.2f, 1.15f, Path.Direction.CW)
            addCircle(18.4f, 11.4f, 1.15f, Path.Direction.CW)
        }
        body.op(dots, Path.Op.DIFFERENCE)

        if (withSlash) {
            // Cut a gap around the slash, then lay the slash itself in the gap.
            body.op(band(3.2f, 3.0f, 20.8f, 21.0f, 2.4f), Path.Op.DIFFERENCE)
            body.op(band(3.2f, 3.0f, 20.8f, 21.0f, 0.95f), Path.Op.UNION)
        }
        return body
    }

    /** A straight band of half-width [hw] along the segment (x0,y0)-(x1,y1). */
    private fun band(x0: Float, y0: Float, x1: Float, y1: Float, hw: Float): Path {
        val len = hypot(x1 - x0, y1 - y0)
        val nx = -(y1 - y0) / len * hw
        val ny = (x1 - x0) / len * hw
        return Path().apply {
            moveTo(x0 + nx, y0 + ny)
            lineTo(x1 + nx, y1 + ny)
            lineTo(x1 - nx, y1 - ny)
            lineTo(x0 - nx, y0 - ny)
            close()
        }
    }
}

/**
 * One square icon button: a 30 dp rounded box (radius 3 dp, white at ~10%
 * so it reads on any game background) with a white glyph centered in it.
 * The view is 45 dp tall (30 dp box + the 15 dp gap that follows it in the
 * reference column), which is also where the optional [badge] — the undo
 * counter — is drawn, so nothing needs to be drawn outside the view bounds.
 */
internal class IconButtonView(context: Context) : View(context) {

    var icon: IconKind? = null
        set(value) {
            field = value
            invalidate()
        }

    var badge: String? = null
        set(value) {
            field = value
            invalidate()
        }

    private val density = resources.displayMetrics.density
    private val boxPx = 30f * density
    private val glyphBoxPx = 19.5f * density // 24 design units -> 19.5 dp

    private val boxPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x1AFFFFFF }
    private val glyphPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFFFFFFF.toInt()
        style = Paint.Style.FILL
    }
    private val badgePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xCCFFFFFF.toInt()
        textSize = 8f * density
        textAlign = Paint.Align.RIGHT
    }
    private val rect = RectF()

    init {
        isClickable = true
        isFocusable = false
        isFocusableInTouchMode = false
    }

    override fun setPressed(pressed: Boolean) {
        super.setPressed(pressed)
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(boxPx.toInt(), (45f * density).toInt())
    }

    override fun onDraw(canvas: Canvas) {
        boxPaint.color = if (isPressed) 0x38FFFFFF else 0x1AFFFFFF
        rect.set(0f, 0f, boxPx, boxPx)
        val radius = 3f * density
        canvas.drawRoundRect(rect, radius, radius, boxPaint)

        icon?.let { kind ->
            val s = glyphBoxPx / 24f
            canvas.save()
            canvas.translate((boxPx - glyphBoxPx) / 2f, (boxPx - glyphBoxPx) / 2f)
            canvas.scale(s, s)
            canvas.drawPath(OverlayIcons.path(kind), glyphPaint)
            canvas.restore()
        }

        badge?.let {
            canvas.drawText(it, boxPx - 2f * density, boxPx - 2f * density, badgePaint)
        }
    }
}

/**
 * The vertical icon column in the top-right corner: four fixed slots, each
 * 30 dp wide, on a 45 dp pitch (30 dp box + 15 dp gap), placed 15 dp from
 * the top/right edge by the host. A slot with no icon is INVISIBLE (keeps
 * the spacing, so e.g. the keyboard icon never jumps when the pencil hides)
 * and, being invisible, doesn't swallow touches meant for the game.
 */
internal class OverlayToolbar(context: Context) : LinearLayout(context) {

    private val slots = List(SLOT_COUNT) { IconButtonView(context) }

    init {
        orientation = VERTICAL
        val d = resources.displayMetrics.density
        slots.forEach { addView(it, LayoutParams((30f * d).toInt(), (45f * d).toInt())) }
        clearAll()
    }

    fun setSlot(index: Int, icon: IconKind?, onClick: (() -> Unit)? = null) {
        val slot = slots[index]
        slot.icon = icon
        slot.badge = null
        slot.visibility = if (icon == null) INVISIBLE else VISIBLE
        slot.setOnClickListener(if (onClick == null) null else View.OnClickListener { onClick() })
        // setOnClickListener(null) would make the view non-clickable; keep it
        // consistent so a hidden/empty slot never behaves oddly when reused.
        slot.isClickable = icon != null
    }

    fun setBadge(index: Int, text: String?) {
        slots[index].badge = text
    }

    fun clearAll() {
        for (i in 0 until SLOT_COUNT) setSlot(i, null)
    }

    companion object {
        const val SLOT_COUNT = 7
        const val SLOT_GAMEPAD = 0
        const val SLOT_EDIT = 1
        const val SLOT_CHEATS = 2
        const val SLOT_KEYBOARD = 3
        const val SLOT_ADD = 3      // edit mode only: "+" (same slot as keyboard in play mode)
        const val SLOT_RESET = 4
        const val SLOT_APPLY_ALL = 5   // edit mode only: directly above the cross
        const val SLOT_EXTRA = 6
    }
}