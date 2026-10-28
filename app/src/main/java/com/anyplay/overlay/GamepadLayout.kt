package com.anyplay.overlay

import android.content.Context
import android.view.KeyEvent
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/**
 * GamepadLayout
 * ----------------
 * Everything the on-screen gamepad needs to know about WHAT is drawn and
 * WHERE: a list of [ControlSpec]s (the D-pad + every button), each with
 * its key, shape, size, opacity, color and screen position.
 *
 * Positions are stored as FRACTIONS of the overlay's width/height (the
 * control's CENTER, 0..1), not pixels — so a saved layout still lands in
 * the same relative spot after rotation or on a different phone.
 *
 * The DEFAULT layout below is measured pixel-by-pixel from the reference
 * screenshots (1600x720 reference frame, 1 reference-px = 0.5333 dp):
 *   - action circles: 85 px  -> 45 dp, ring 4 px -> 2 dp, color #444444
 *     at 70% opacity, label cap-height 15 px -> ~11.25 dp text
 *   - D-pad: 234 px square -> 125 dp, arms 58 px wide
 *   - centers: D-pad (214,547), ENTER (1370,483), ESCAPE (1487,483),
 *     Z (1370,596), B (1487,596) -> the fractions used below.
 */
enum class ControlType { DPAD, BUTTON }

enum class ControlShape(val title: String) {
    CIRCLE("Circle"),
    SQUARE("Square"),
    ROUNDED("Rounded"),
    OVAL("Oval");

    /** Circle/Square always keep width == height. */
    val locksRatio: Boolean get() = this == CIRCLE || this == SQUARE
}

data class ControlSpec(
    val id: String,
    val type: ControlType,
    val label: String,
    val keyCode: Int,
    val shape: ControlShape,
    val widthDp: Float,
    val heightDp: Float,
    val textDp: Float,
    /** 0..1 */
    val opacity: Float,
    /** RGB only (alpha bits ignored). */
    val color: Int,
    /** true = tap once to hold, tap again to release. */
    val latch: Boolean,
    /** Center position as a fraction of the overlay (0..1). */
    val cx: Float,
    val cy: Float
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("id", id)
        put("type", type.name)
        put("label", label)
        put("keyCode", keyCode)
        put("shape", shape.name)
        put("w", widthDp.toDouble())
        put("h", heightDp.toDouble())
        put("text", textDp.toDouble())
        put("opacity", opacity.toDouble())
        put("color", color and 0xFFFFFF)
        put("latch", latch)
        put("cx", cx.toDouble())
        put("cy", cy.toDouble())
    }

    companion object {
        fun fromJson(o: JSONObject): ControlSpec = ControlSpec(
            id = o.getString("id"),
            type = ControlType.valueOf(o.getString("type")),
            label = o.optString("label", ""),
            keyCode = o.optInt("keyCode", KeyEvent.KEYCODE_UNKNOWN),
            shape = runCatching { ControlShape.valueOf(o.getString("shape")) }
                .getOrDefault(ControlShape.CIRCLE),
            widthDp = o.optDouble("w", 45.0).toFloat(),
            heightDp = o.optDouble("h", 45.0).toFloat(),
            textDp = o.optDouble("text", 11.25).toFloat(),
            opacity = o.optDouble("opacity", 0.7).toFloat().coerceIn(0.05f, 1f),
            color = o.optInt("color", 0x444444) and 0xFFFFFF,
            latch = o.optBoolean("latch", false),
            cx = o.optDouble("cx", 0.5).toFloat().coerceIn(0f, 1f),
            cy = o.optDouble("cy", 0.5).toFloat().coerceIn(0f, 1f)
        )
    }
}

data class GamepadLayout(val controls: List<ControlSpec>) {

    fun find(id: String): ControlSpec? = controls.firstOrNull { it.id == id }

    fun replace(spec: ControlSpec): GamepadLayout =
        GamepadLayout(controls.map { if (it.id == spec.id) spec else it })

    fun remove(id: String): GamepadLayout =
        GamepadLayout(controls.filterNot { it.id == id })

    fun add(spec: ControlSpec): GamepadLayout = GamepadLayout(controls + spec)

    fun toJson(): String {
        val arr = JSONArray()
        controls.forEach { arr.put(it.toJson()) }
        return JSONObject().put("version", 1).put("controls", arr).toString()
    }

    companion object {
        const val DPAD_ID = "dpad"

        fun newId(): String = "c_" + UUID.randomUUID().toString().take(8)

        fun fromJson(json: String): GamepadLayout? = try {
            val arr = JSONObject(json).getJSONArray("controls")
            val list = (0 until arr.length()).map { ControlSpec.fromJson(arr.getJSONObject(it)) }
            if (list.isEmpty()) null else GamepadLayout(list)
        } catch (e: Exception) {
            null
        }

        private fun button(
            id: String,
            label: String,
            keyCode: Int,
            cx: Float,
            cy: Float
        ) = ControlSpec(
            id = id,
            type = ControlType.BUTTON,
            label = label,
            keyCode = keyCode,
            shape = ControlShape.CIRCLE,
            widthDp = 45f,
            heightDp = 45f,
            textDp = 11.25f,
            opacity = 0.70f,
            color = 0x444444,
            latch = false,
            cx = cx,
            cy = cy
        )

        /** A fresh button for the "+" icon: same look as the default buttons, user then edits it in the dialog. */
        fun newButton(): ControlSpec = button(newId(), "A", KeyEvent.KEYCODE_A, 0.5f, 0.5f)

        fun default(): GamepadLayout = GamepadLayout(
            listOf(
                ControlSpec(
                    id = DPAD_ID,
                    type = ControlType.DPAD,
                    label = "",
                    keyCode = KeyEvent.KEYCODE_UNKNOWN,
                    shape = ControlShape.SQUARE,
                    widthDp = 125f,
                    heightDp = 125f,
                    textDp = 0f,
                    opacity = 0.90f,
                    color = 0x222222,
                    latch = false,
                    cx = 0.13375f,
                    cy = 0.7597f
                ),
                button("enter", "ENTER", KeyEvent.KEYCODE_ENTER, 0.8566f, 0.6715f),
                button("escape", "ESCAPE", KeyEvent.KEYCODE_ESCAPE, 0.9297f, 0.6715f),
                button("z", "Z", KeyEvent.KEYCODE_Z, 0.8566f, 0.8278f),
                button("b", "B", KeyEvent.KEYCODE_B, 0.9297f, 0.8278f)
            )
        )
    }
}

/**
 * Saves/loads the layout PER GAME (keyed by the game's folder URI, the same
 * key the cheats use). A game with no saved layout starts from the default;
 * once saved with the pencil's check icon it stays exactly like that across
 * closing the game or the whole app, until it is edited again.
 */
class GamepadLayoutStore(context: Context, gameKey: String) {
    private val prefs = context.applicationContext
        .getSharedPreferences("anyplay_gamepad_layouts", Context.MODE_PRIVATE)

    // "layout2_": layouts saved by the first version had the wrong default
    // keys (ENTER/ESCAPE/Z/B went through the old InputMapper table), so
    // they are deliberately not read any more.
    private val layoutKey = LAYOUT_PREFIX + sha1(gameKey)
    private val historyKey = HISTORY_PREFIX + sha1(gameKey)

    /** This game's own saved layout, else the "apply to all" layout, else the app default. */
    fun load(): GamepadLayout =
        prefs.getString(layoutKey, null)?.let { GamepadLayout.fromJson(it) }
            ?: prefs.getString(GLOBAL_KEY, null)?.let { GamepadLayout.fromJson(it) }
            ?: GamepadLayout.default()

    /**
     * "Apply to all": makes [layout] the layout of EVERY game — games that
     * already have a saved layout are overwritten, and games opened later
     * (no saved layout yet) start from it too. For each overwritten game the
     * layout it had before is pushed onto its undo history, so opening the
     * pencil there shows one more undo step and undo brings the old layout
     * back. The app default (reset button) is not touched.
     */
    fun applyToAll(layout: GamepadLayout) {
        val newJson = layout.toJson()
        val editor = prefs.edit()
        for (key in prefs.all.keys.filter { it.startsWith(LAYOUT_PREFIX) }) {
            val old = prefs.getString(key, null) ?: continue
            if (old == newJson) continue
            val histKey = HISTORY_PREFIX + key.removePrefix(LAYOUT_PREFIX)
            val hist = try {
                val arr = org.json.JSONArray(prefs.getString(histKey, "[]"))
                (0 until arr.length()).map { arr.getString(it) }
            } catch (e: Exception) {
                emptyList()
            }
            val out = org.json.JSONArray()
            (hist + old).takeLast(MAX_HISTORY).forEach { out.put(it) }
            editor.putString(histKey, out.toString())
            editor.putString(key, newJson)
        }
        editor.putString(layoutKey, newJson)
        editor.putString(GLOBAL_KEY, newJson)
        editor.commit()
    }

    fun save(layout: GamepadLayout) {
        // commit() (synchronous) so the layout survives even if the app is
        // killed right after pressing the check icon.
        prefs.edit().putString(layoutKey, layout.toJson()).commit()
    }

    /**
     * The undo history, oldest first. Kept per game across closing the game
     * or the app, so pressing the pencil later can still undo changes that
     * were saved with the check icon earlier (last [MAX_HISTORY] steps).
     */
    fun loadHistory(): List<String> = try {
        val arr = org.json.JSONArray(prefs.getString(historyKey, "[]"))
        (0 until arr.length()).map { arr.getString(it) }
    } catch (e: Exception) {
        emptyList()
    }

    fun saveHistory(history: List<String>) {
        val arr = org.json.JSONArray()
        history.takeLast(MAX_HISTORY).forEach { arr.put(it) }
        prefs.edit().putString(historyKey, arr.toString()).commit()
    }

    fun reset() {
        prefs.edit().remove(layoutKey).remove(historyKey).commit()
    }

    private fun sha1(text: String): String =
        java.security.MessageDigest.getInstance("SHA-1")
            .digest(text.toByteArray())
            .joinToString("") { "%02x".format(it) }

    private companion object {
        const val LAYOUT_PREFIX = "layout2_"
        const val HISTORY_PREFIX = "history2_"
        const val GLOBAL_KEY = "global_layout2"
        const val MAX_HISTORY = 50
    }
}

/** Human-readable key names for the key picker in the edit dialog. */
object KeyCatalog {
    data class Entry(val name: String, val keyCode: Int)

    val entries: List<Entry> by lazy {
        val list = mutableListOf<Entry>()
        list += Entry("ENTER", KeyEvent.KEYCODE_ENTER)
        list += Entry("ESCAPE", KeyEvent.KEYCODE_ESCAPE)
        list += Entry("SPACE", KeyEvent.KEYCODE_SPACE)
        list += Entry("TAB", KeyEvent.KEYCODE_TAB)
        list += Entry("BACKSPACE", KeyEvent.KEYCODE_DEL)
        list += Entry("SHIFT", KeyEvent.KEYCODE_SHIFT_LEFT)
        list += Entry("CTRL", KeyEvent.KEYCODE_CTRL_LEFT)
        list += Entry("ALT", KeyEvent.KEYCODE_ALT_LEFT)
        list += Entry("UP", KeyEvent.KEYCODE_DPAD_UP)
        list += Entry("DOWN", KeyEvent.KEYCODE_DPAD_DOWN)
        list += Entry("LEFT", KeyEvent.KEYCODE_DPAD_LEFT)
        list += Entry("RIGHT", KeyEvent.KEYCODE_DPAD_RIGHT)
        list += Entry("PAGE UP", KeyEvent.KEYCODE_PAGE_UP)
        list += Entry("PAGE DOWN", KeyEvent.KEYCODE_PAGE_DOWN)
        list += Entry("HOME", KeyEvent.KEYCODE_MOVE_HOME)
        list += Entry("END", KeyEvent.KEYCODE_MOVE_END)
        list += Entry("INSERT", KeyEvent.KEYCODE_INSERT)
        list += Entry("DELETE", KeyEvent.KEYCODE_FORWARD_DEL)
        for (i in 0 until 26) list += Entry(('A' + i).toString(), KeyEvent.KEYCODE_A + i)
        for (i in 0 until 10) list += Entry(i.toString(), KeyEvent.KEYCODE_0 + i)
        for (i in 1..12) list += Entry("F$i", KeyEvent.KEYCODE_F1 + i - 1)
        list += Entry("-", KeyEvent.KEYCODE_MINUS)
        list += Entry("=", KeyEvent.KEYCODE_EQUALS)
        list += Entry("[", KeyEvent.KEYCODE_LEFT_BRACKET)
        list += Entry("]", KeyEvent.KEYCODE_RIGHT_BRACKET)
        list += Entry(";", KeyEvent.KEYCODE_SEMICOLON)
        list += Entry("'", KeyEvent.KEYCODE_APOSTROPHE)
        list += Entry(",", KeyEvent.KEYCODE_COMMA)
        list += Entry(".", KeyEvent.KEYCODE_PERIOD)
        list += Entry("/", KeyEvent.KEYCODE_SLASH)
        list += Entry("\\", KeyEvent.KEYCODE_BACKSLASH)
        list += Entry("`", KeyEvent.KEYCODE_GRAVE)
        list
    }

    fun nameOf(keyCode: Int): String =
        entries.firstOrNull { it.keyCode == keyCode }?.name
            ?: KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_").replace('_', ' ')
}