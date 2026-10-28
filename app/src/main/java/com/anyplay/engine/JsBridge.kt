package com.anyplay.engine

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.KeyEvent
import org.libsdl.app.SDLActivity

/** Direction buttons used by the D-pad control. */
enum class GamepadButton { UP, DOWN, LEFT, RIGHT }

object InputMapper {
    fun keyCodeFor(button: GamepadButton): Int = when (button) {
        GamepadButton.UP -> KeyEvent.KEYCODE_DPAD_UP
        GamepadButton.DOWN -> KeyEvent.KEYCODE_DPAD_DOWN
        GamepadButton.LEFT -> KeyEvent.KEYCODE_DPAD_LEFT
        GamepadButton.RIGHT -> KeyEvent.KEYCODE_DPAD_RIGHT
    }
}

/**
 * In the AnyPlay app this sends keys into a WebView. In the RGSS plugin it sends
 * them straight into SDL (the game engine), as if a real keyboard was used.
 *
 * A key is always held for at least MIN_HOLD_MS, because the game only looks at
 * the keyboard once per frame (40 fps) and a very short tap could be missed.
 */
class JsBridge {
    private val handler = Handler(Looper.getMainLooper())
    private val downAt = HashMap<Int, Long>()

    fun sendKeyDown(code: Int, meta: Int = 0) {
        if (meta and KeyEvent.META_SHIFT_ON != 0) SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_SHIFT_LEFT)
        if (meta and KeyEvent.META_CTRL_ON != 0) SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_CTRL_LEFT)
        if (meta and KeyEvent.META_ALT_ON != 0) SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_ALT_LEFT)
        downAt[code] = SystemClock.uptimeMillis()
        SDLActivity.onNativeKeyDown(code)
    }

    fun sendKeyUp(code: Int, meta: Int = 0) {
        val held = SystemClock.uptimeMillis() - (downAt.remove(code) ?: 0L)
        val wait = (MIN_HOLD_MS - held).coerceAtLeast(0L)
        val release = Runnable {
            SDLActivity.onNativeKeyUp(code)
            if (meta and KeyEvent.META_ALT_ON != 0) SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_ALT_LEFT)
            if (meta and KeyEvent.META_CTRL_ON != 0) SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_CTRL_LEFT)
            if (meta and KeyEvent.META_SHIFT_ON != 0) SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_SHIFT_LEFT)
        }
        if (wait == 0L) release.run() else handler.postDelayed(release, wait)
    }

    /** A quick press-and-release (used by the "tap the screen" layer). */
    fun tap(code: Int) {
        sendKeyDown(code)
        sendKeyUp(code)
    }

    companion object {
        const val MIN_HOLD_MS = 70L
    }
}
