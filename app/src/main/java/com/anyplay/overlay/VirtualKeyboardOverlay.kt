package com.anyplay.overlay

import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import com.anyplay.engine.JsBridge
import kotlin.math.min

/**
 * One key on the virtual keyboard: what is printed on it, which Android
 * keyCode it sends, its relative width within its row (1f = a normal
 * letter key) and — for the modifier keys only — the meta-state bits that
 * key contributes to every OTHER key pressed while it is latched.
 *
 * A modifier ([metaMask] != 0) LATCHES: tap it once and it stays down
 * (highlighted) until tapped again, so Shift/Ctrl/Alt can be combined
 * with a following tap using a single finger. Every other key is an
 * ordinary press-and-hold: down while the finger is on it, up when the
 * finger lifts.
 */
private data class KeySpec(
    val label: String,
    val keyCode: Int,
    val weight: Float = 1f,
    val metaMask: Int = 0
) {
    val isModifier: Boolean get() = metaMask != 0
}

/**
 * VirtualKeyboardOverlay
 * ------------------------
 * The QWERTY overlay, restyled to match the reference app pixel-for-pixel
 * (measured from its screenshot; 1 reference-px = 0.5333 dp):
 *
 *   - a translucent light-gray rounded panel, 475 x 190 dp, centered at the
 *     bottom (16 dp above the edge), padding 7.5 dp,
 *   - keys 25 x 30 dp with a 5 dp gap (a "1x" key; wider keys use the
 *     reference's proportions: Tab/Enter/Backspace 2.34, Caps/Ctrl 2.0,
 *     Shift 2.66, Alt 1.66, ...), dark gray (#535353 at ~78% on top of the panel) with a 5 dp
 *     corner radius and white 12 dp labels,
 *   - labels in the reference's style (capital letters, "Tab", "Caps",
 *     "Enter", "Shift", "Ctrl", "Alt", "Space").
 *
 * The key SET is still ours (arrow keys in the bottom row instead of the
 * reference's Fn / Mn), and all key behavior — press-and-hold, latching
 * modifiers with meta-state, releasing everything when hidden — is the same
 * logic as before; only looks and sizes changed.
 */
class VirtualKeyboardOverlay(
    context: Context,
    private val jsBridge: JsBridge
) : LinearLayout(context) {

    /** Shared by every key on this keyboard — see ModifierLatch. */
    private val modifiers = ModifierLatch()

    private val density = resources.displayMetrics.density

    init {
        orientation = VERTICAL
        // Panel: #D2D2D2 at 50% (regressed from the with/without-keyboard
        // screenshots), 10 dp corners.
        background = GradientDrawable().apply {
            setColor(Color.argb(0x80, 0xD2, 0xD2, 0xD2))
            cornerRadius = 10f * density
        }
        val pad = (7.5f * density).toInt()
        setPadding(pad, pad, pad, pad)
        // Taps landing in the gaps BETWEEN keys must stop here rather than
        // falling through to the WebView underneath — a stray tap on the game
        // canvas is a real in-game action for most RPG Maker games.
        isClickable = true
        val margin = (2.5f * density).toInt()

        for (row in KEYBOARD_LAYOUT) {
            val rowLayout = LinearLayout(context).apply { orientation = HORIZONTAL }
            // height = 0 with weight = 1 on every row: Android divides the
            // panel's inner height evenly over the 5 rows (35 dp each = 30 dp
            // key + 5 dp gap), so the keyboard always fills its panel exactly.
            addView(rowLayout, LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f))

            for (key in row) {
                val keyView = KeyButton(context, jsBridge, key, modifiers)
                val params = LayoutParams(0, LayoutParams.MATCH_PARENT, key.weight)
                params.setMargins(margin, margin, margin, margin)
                rowLayout.addView(keyView, params)
            }
        }
    }

    /**
     * Fixed 190 dp tall; 475 dp wide, but never wider than the screen minus
     * 8 dp on each side (so it still fits narrow phones — the key weights
     * simply shrink the keys proportionally).
     */
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val available = MeasureSpec.getSize(widthMeasureSpec)
        val wanted = (PANEL_WIDTH_DP * density).toInt()
        val width = if (available > 0) min(wanted, (available - 16f * density).toInt().coerceAtLeast(1)) else wanted
        super.onMeasure(
            MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
            MeasureSpec.makeMeasureSpec((PANEL_HEIGHT_DP * density).toInt(), MeasureSpec.EXACTLY)
        )
    }

    /** Call from GamePlayActivity.onPause() or when hiding this overlay —
     *  releases every key still logically down (a letter whose finger is
     *  still on it, or any latched modifier), so nothing gets stuck down
     *  in the game. */
    fun releaseAllHeldKeys() {
        for (i in 0 until childCount) {
            val row = getChildAt(i) as? ViewGroup ?: continue
            for (j in 0 until row.childCount) {
                (row.getChildAt(j) as? KeyButton)?.releaseIfHeld()
            }
        }
    }

    companion object {
        const val PANEL_WIDTH_DP = 475f
        const val PANEL_HEIGHT_DP = 190f

        // Widths below are the reference's, in "key units" (a letter key = 1).
        // Every row adds up to ~15.34 so all rows end flush on the right.
        private val KEYBOARD_LAYOUT: List<List<KeySpec>> = listOf(
            listOf(
                KeySpec("`", KeyEvent.KEYCODE_GRAVE),
                KeySpec("1", KeyEvent.KEYCODE_1),
                KeySpec("2", KeyEvent.KEYCODE_2),
                KeySpec("3", KeyEvent.KEYCODE_3),
                KeySpec("4", KeyEvent.KEYCODE_4),
                KeySpec("5", KeyEvent.KEYCODE_5),
                KeySpec("6", KeyEvent.KEYCODE_6),
                KeySpec("7", KeyEvent.KEYCODE_7),
                KeySpec("8", KeyEvent.KEYCODE_8),
                KeySpec("9", KeyEvent.KEYCODE_9),
                KeySpec("0", KeyEvent.KEYCODE_0),
                KeySpec("-", KeyEvent.KEYCODE_MINUS),
                KeySpec("=", KeyEvent.KEYCODE_EQUALS),
                KeySpec("\u232B", KeyEvent.KEYCODE_DEL, weight = 2.34f)
            ),
            listOf(
                KeySpec("Tab", KeyEvent.KEYCODE_TAB, weight = 2.34f),
                KeySpec("Q", KeyEvent.KEYCODE_Q),
                KeySpec("W", KeyEvent.KEYCODE_W),
                KeySpec("E", KeyEvent.KEYCODE_E),
                KeySpec("R", KeyEvent.KEYCODE_R),
                KeySpec("T", KeyEvent.KEYCODE_T),
                KeySpec("Y", KeyEvent.KEYCODE_Y),
                KeySpec("U", KeyEvent.KEYCODE_U),
                KeySpec("I", KeyEvent.KEYCODE_I),
                KeySpec("O", KeyEvent.KEYCODE_O),
                KeySpec("P", KeyEvent.KEYCODE_P),
                KeySpec("[", KeyEvent.KEYCODE_LEFT_BRACKET),
                KeySpec("]", KeyEvent.KEYCODE_RIGHT_BRACKET),
                KeySpec("\\", KeyEvent.KEYCODE_BACKSLASH)
            ),
            listOf(
                // Caps is a latching modifier like Shift rather than a
                // momentary tap: a tapped KEYCODE_CAPS_LOCK does nothing at all
                // to a synthetic event stream (nothing in between tracks a lock
                // state), so the only way it can affect the keys typed after it
                // is by contributing META_CAPS_LOCK_ON to them while latched.
                KeySpec("Caps", KeyEvent.KEYCODE_CAPS_LOCK, weight = 2f,
                    metaMask = KeyEvent.META_CAPS_LOCK_ON),
                KeySpec("A", KeyEvent.KEYCODE_A),
                KeySpec("S", KeyEvent.KEYCODE_S),
                KeySpec("D", KeyEvent.KEYCODE_D),
                KeySpec("F", KeyEvent.KEYCODE_F),
                KeySpec("G", KeyEvent.KEYCODE_G),
                KeySpec("H", KeyEvent.KEYCODE_H),
                KeySpec("J", KeyEvent.KEYCODE_J),
                KeySpec("K", KeyEvent.KEYCODE_K),
                KeySpec("L", KeyEvent.KEYCODE_L),
                KeySpec(";", KeyEvent.KEYCODE_SEMICOLON),
                KeySpec("'", KeyEvent.KEYCODE_APOSTROPHE),
                KeySpec("Enter", KeyEvent.KEYCODE_ENTER, weight = 2.34f)
            ),
            listOf(
                KeySpec("Shift", KeyEvent.KEYCODE_SHIFT_LEFT, weight = 2.66f,
                    metaMask = KeyEvent.META_SHIFT_ON or KeyEvent.META_SHIFT_LEFT_ON),
                KeySpec("Z", KeyEvent.KEYCODE_Z),
                KeySpec("X", KeyEvent.KEYCODE_X),
                KeySpec("C", KeyEvent.KEYCODE_C),
                KeySpec("V", KeyEvent.KEYCODE_V),
                KeySpec("B", KeyEvent.KEYCODE_B),
                KeySpec("N", KeyEvent.KEYCODE_N),
                KeySpec("M", KeyEvent.KEYCODE_M),
                KeySpec(",", KeyEvent.KEYCODE_COMMA),
                KeySpec(".", KeyEvent.KEYCODE_PERIOD),
                KeySpec("/", KeyEvent.KEYCODE_SLASH),
                KeySpec("Shift", KeyEvent.KEYCODE_SHIFT_RIGHT, weight = 2.66f,
                    metaMask = KeyEvent.META_SHIFT_ON or KeyEvent.META_SHIFT_RIGHT_ON)
            ),
            listOf(
                KeySpec("Ctrl", KeyEvent.KEYCODE_CTRL_LEFT, weight = 2f,
                    metaMask = KeyEvent.META_CTRL_ON or KeyEvent.META_CTRL_LEFT_ON),
                KeySpec("Alt", KeyEvent.KEYCODE_ALT_LEFT, weight = 1.66f,
                    metaMask = KeyEvent.META_ALT_ON or KeyEvent.META_ALT_LEFT_ON),
                // Space is intentionally by far the widest key, like a real keyboard.
                KeySpec("Space", KeyEvent.KEYCODE_SPACE, weight = 6.32f),
                // Our arrow keys (kept as-is; the reference has Fn / Mn here).
                KeySpec("\u2190", KeyEvent.KEYCODE_DPAD_LEFT, weight = 1.34f),
                KeySpec("\u2191", KeyEvent.KEYCODE_DPAD_UP, weight = 1.34f),
                KeySpec("\u2193", KeyEvent.KEYCODE_DPAD_DOWN, weight = 1.34f),
                KeySpec("\u2192", KeyEvent.KEYCODE_DPAD_RIGHT, weight = 1.34f)
            )
        )
    }
}

/**
 * Which modifier keys are currently latched, shared by every KeyButton on
 * one keyboard.
 *
 * A synthetic KeyEvent carries its own modifier state — dispatching a
 * Shift-down event first does NOT make a later letter event uppercase,
 * because nothing between the two remembers that shift went down. So the
 * latched modifiers have to be folded into the metaState of every key
 * dispatched while they are held, which is what [metaState] is for.
 */
private class ModifierLatch {
    private val latched = mutableSetOf<KeySpec>()

    val metaState: Int
        get() = latched.fold(0) { acc, spec -> acc or spec.metaMask }

    fun isLatched(spec: KeySpec) = spec in latched
    fun latch(spec: KeySpec) { latched.add(spec) }
    fun unlatch(spec: KeySpec) { latched.remove(spec) }
}

/**
 * A single key.
 *
 * Non-modifier keys are press-and-hold: key down when the finger touches,
 * key up when it lifts. They are deliberately NOT an instant down+up pair
 * — game engines poll input once per animation frame (RPG Maker's
 * Input.update() being the canonical case) instead of reacting to keydown
 * as it arrives, so a key that goes down and back up within the same frame
 * is never observed as pressed.
 *
 * Modifier keys (Shift/Ctrl/Alt/Caps) latch instead: tap once to hold,
 * tap again to release, so they can be combined with a following key
 * using one finger.
 */
private class KeyButton(
    context: Context,
    private val jsBridge: JsBridge,
    private val spec: KeySpec,
    private val modifiers: ModifierLatch
) : TextView(context) {

    private var isHeld = false

    // Measured from the reference: against the plain game screen the key
    // area composites to about 88 + 0.108 * background. A key is drawn ON TOP
    // of the translucent panel, so solving for that stack gives #535353 at
    // ~78% (an earlier version used the composite value directly, which made
    // keys too light). 5 dp corners. The pressed shade is a lighter gray of
    // the same family (the reference screenshot shows no pressed key).
    private val idleDrawable = GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        setColor(Color.argb(0xC8, 0x53, 0x53, 0x53))
        cornerRadius = 5f * resources.displayMetrics.density
    }
    private val heldDrawable = GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        setColor(Color.argb(0xE0, 0x8A, 0x8A, 0x8A))
        cornerRadius = 5f * resources.displayMetrics.density
    }

    init {
        // Touching a key must NEVER pull Android focus away from the
        // WebView (a focus-less WebView can silently drop dispatched KeyEvents).
        isFocusable = false
        isFocusableInTouchMode = false
        text = spec.label
        gravity = Gravity.CENTER
        setTextColor(Color.WHITE)
        setTextSize(TypedValue.COMPLEX_UNIT_DIP, 12f)
        isSingleLine = true
        includeFontPadding = false
        setPadding(0, 0, 0, 0)
        background = idleDrawable
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if (spec.isModifier) {
                    // Latching: the DOWN toggles it, and the matching UP
                    // does nothing — it stays down until tapped again.
                    if (modifiers.isLatched(spec)) releaseIfHeld() else press()
                } else {
                    press()
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                // A latched modifier survives its own finger lifting;
                // everything else goes up with the finger.
                if (!spec.isModifier) releaseIfHeld()
                return true
            }
        }
        return false
    }

    private fun press() {
        if (isHeld) return
        isHeld = true
        if (spec.isModifier) modifiers.latch(spec)
        // Meta-state is read AFTER latching so a modifier's own event
        // also carries its bit, exactly as a hardware keyboard reports it.
        jsBridge.sendKeyDown(spec.keyCode, modifiers.metaState)
        background = heldDrawable
    }

    fun releaseIfHeld() {
        if (isHeld) {
            isHeld = false
            // Read before unlatching, so the modifier's own key-up still
            // reports itself as down — same as a hardware keyboard.
            val meta = modifiers.metaState
            if (spec.isModifier) modifiers.unlatch(spec)
            jsBridge.sendKeyUp(spec.keyCode, meta)
        }
        background = idleDrawable
    }
}