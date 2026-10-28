package com.anyplay.overlay

import android.content.Context
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import com.anyplay.engine.JsBridge

/**
 * OverlayController
 * -------------------
 * Replaces the two "+"/"−" FloatingToggleButtons with the reference app's
 * behavior: ONE icon column in the top-right corner whose icons change with
 * the current [Mode]:
 *
 *   COLLAPSED  game just started: only the gamepad icon.
 *   EXPANDED   gamepad shown. Icons: crossed gamepad (collapse), pencil
 *              (edit layout), sparkle (cheats), keyboard.
 *   KEYBOARD   keyboard shown, gamepad + pencil hidden. Icons: crossed
 *              gamepad (collapse everything), sparkle, keyboard (back).
 *   EDIT       grid + draggable buttons. Icons: check (save), undo (with a
 *              change counter under it), reset-to-default, cross (discard).
 *
 * The undo counter works like the reference: every finished change (a drag,
 * a released slider, a picked color, copy/delete ...) is ONE step; moving a
 * button 3 times shows 3; each undo reverts the last step and the counter
 * counts down.
 *
 * GamePlayActivity only creates this, forwards back-press / pause, and
 * supplies what the sparkle icon does (open the cheats panel).
 */
class OverlayController(
    private val context: Context,
    container: FrameLayout,
    bridge: JsBridge,
    gameKey: String,
    private val onOpenCheats: () -> Unit,
    /** false for non-RPG-Maker games: the cheats (sparkle) icon is not shown. */
    private val showCheats: Boolean = true
) {

    private enum class Mode { COLLAPSED, EXPANDED, KEYBOARD, EDIT }

    private val density = context.resources.displayMetrics.density
    private val store = GamepadLayoutStore(context, gameKey)

    val gamepad: VirtualGamepadOverlay = VirtualGamepadOverlay(context, bridge, store.load())
    val keyboard: VirtualKeyboardOverlay = VirtualKeyboardOverlay(context, bridge)
    private val toolbar = OverlayToolbar(context)

    private var mode = Mode.COLLAPSED

    /** Without the cheats icon, the keyboard toggle moves up into the cheats slot so
     *  no empty gap is left between the pencil and the keyboard icon. Same slot in
     *  every mode, so the icon never jumps. (Edit mode sets its own slots explicitly.) */
    private val keyboardSlot: Int
        get() = if (showCheats) OverlayToolbar.SLOT_KEYBOARD else OverlayToolbar.SLOT_CHEATS

    // Edit session state.
    private var sessionStartLayout: GamepadLayout? = null
    private val undoStack = ArrayDeque<String>()
    private var baselineJson = ""
    private var editDialog: ControlEditDialog? = null

    init {
        gamepad.visibility = View.GONE
        keyboard.visibility = View.GONE

        container.addView(
            gamepad,
            FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        )
        container.addView(
            keyboard,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
            ).apply { bottomMargin = (16f * density).toInt() }
        )
        // 15 dp from the top and right edges, like the reference.
        container.addView(
            toolbar,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP or Gravity.END
            ).apply {
                topMargin = (15f * density).toInt()
                rightMargin = (15f * density).toInt()
            }
        )

        gamepad.onControlTapped = { id -> openControlDialog(id) }
        gamepad.onLayoutEdited = { commitChange() }
        render()
    }

    // ---- Public hooks for the Activity ----------------------------------

    /** Back pressed: returns true if it was consumed (edit mode -> discard edits). */
    fun handleBack(): Boolean {
        if (mode == Mode.EDIT) {
            cancelEdit()
            return true
        }
        return false
    }

    fun releaseAllKeys() {
        gamepad.releaseAllHeldKeys()
        keyboard.releaseAllHeldKeys()
    }

    /** Dismisses any open dialog; call before the Activity/session goes away. */
    fun shutdown() {
        editDialog?.dismiss()
        editDialog = null
        releaseAllKeys()
    }

    // ---- Modes -----------------------------------------------------------

    private fun setMode(newMode: Mode) {
        mode = newMode
        render()
    }

    private fun render() {
        toolbar.clearAll()
        when (mode) {
            Mode.COLLAPSED -> {
                showGamepad(false)
                showKeyboard(false)
                toolbar.setSlot(OverlayToolbar.SLOT_GAMEPAD, IconKind.GAMEPAD) { setMode(Mode.EXPANDED) }
            }

            Mode.EXPANDED -> {
                showGamepad(true)
                showKeyboard(false)
                toolbar.setSlot(OverlayToolbar.SLOT_GAMEPAD, IconKind.GAMEPAD_OFF) { setMode(Mode.COLLAPSED) }
                toolbar.setSlot(OverlayToolbar.SLOT_EDIT, IconKind.PENCIL) { startEdit() }
                if (showCheats) toolbar.setSlot(OverlayToolbar.SLOT_CHEATS, IconKind.STAR) { onOpenCheats() }
                toolbar.setSlot(keyboardSlot, IconKind.KEYBOARD) { setMode(Mode.KEYBOARD) }
            }

            Mode.KEYBOARD -> {
                showGamepad(false)
                showKeyboard(true)
                toolbar.setSlot(OverlayToolbar.SLOT_GAMEPAD, IconKind.GAMEPAD_OFF) { setMode(Mode.COLLAPSED) }
                if (showCheats) toolbar.setSlot(OverlayToolbar.SLOT_CHEATS, IconKind.STAR) { onOpenCheats() }
                toolbar.setSlot(keyboardSlot, IconKind.KEYBOARD) { setMode(Mode.EXPANDED) }
            }

            Mode.EDIT -> {
                showGamepad(true)
                showKeyboard(false)
                toolbar.setSlot(OverlayToolbar.SLOT_EDIT, IconKind.CHECK) { saveEdit() }
                toolbar.setSlot(OverlayToolbar.SLOT_CHEATS, IconKind.UNDO) { undo() }
                toolbar.setSlot(OverlayToolbar.SLOT_ADD, IconKind.ADD) { addControl() }
                toolbar.setSlot(OverlayToolbar.SLOT_RESET, IconKind.RESET) { resetLayout() }
                toolbar.setSlot(OverlayToolbar.SLOT_APPLY_ALL, IconKind.APPLY_ALL) { confirmApplyToAll() }
                toolbar.setSlot(OverlayToolbar.SLOT_EXTRA, IconKind.CLOSE) { cancelEdit() }
                toolbar.setBadge(OverlayToolbar.SLOT_CHEATS, undoStack.size.toString())
            }
        }
    }

    private fun showGamepad(show: Boolean) {
        if (!show) gamepad.releaseAllHeldKeys()
        gamepad.visibility = if (show) View.VISIBLE else View.GONE
    }

    private fun showKeyboard(show: Boolean) {
        if (!show) keyboard.releaseAllHeldKeys()
        keyboard.visibility = if (show) View.VISIBLE else View.GONE
    }

    // ---- Edit mode --------------------------------------------------------

    private fun startEdit() {
        sessionStartLayout = gamepad.layout
        // Undo history of earlier saved edits comes back too (per game).
        undoStack.clear()
        undoStack.addAll(store.loadHistory())
        baselineJson = gamepad.layout.toJson()
        gamepad.editMode = true
        setMode(Mode.EDIT)
    }

    /** Records the change made since the last step as ONE undo step. */
    private fun commitChange() {
        val now = gamepad.layout.toJson()
        if (now == baselineJson) return
        undoStack.addLast(baselineJson)
        baselineJson = now
        updateUndoBadge()
    }

    private fun undo() {
        if (undoStack.isEmpty()) return
        val previous = undoStack.removeLast()
        GamepadLayout.fromJson(previous)?.let { gamepad.layout = it }
        baselineJson = previous
        updateUndoBadge()
    }

    /** "+" icon: adds a new button (ONE undo step) and opens the same edit dialog
     *  that Copy/tap uses, so the user can pick key, shape, size, color right away. */
    private fun addControl() {
        val newSpec = GamepadLayout.newButton()
        gamepad.layout = gamepad.layout.add(newSpec)
        commitChange()
        openControlDialog(newSpec.id)
    }

    /** Puts every button (and the D-pad) back to the default spot/size/color/key.
     *  It is ONE undo step, so a reset can itself be undone. */
    private fun resetLayout() {
        gamepad.layout = GamepadLayout.default()
        commitChange()
    }

    private fun updateUndoBadge() {
        if (mode == Mode.EDIT) toolbar.setBadge(OverlayToolbar.SLOT_CHEATS, undoStack.size.toString())
    }

    private fun saveEdit() {
        editDialog?.dismiss()
        store.save(gamepad.layout)
        store.saveHistory(undoStack.toList())
        endEdit()
    }

    private fun confirmApplyToAll() {
        android.app.AlertDialog.Builder(context)
            .setTitle("Apply to all games?")
            .setMessage(
                "This layout will replace the gamepad layout of every game (also games you open later). " +
                        "In each game it counts as one undo step."
            )
            .setPositiveButton("Apply") { _, _ -> applyToAll() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    /** Saves this game like the check icon, then copies the layout to every other game. */
    private fun applyToAll() {
        editDialog?.dismiss()
        store.save(gamepad.layout)
        store.saveHistory(undoStack.toList())
        store.applyToAll(gamepad.layout)
        endEdit()
    }

    private fun cancelEdit() {
        editDialog?.dismiss()
        sessionStartLayout?.let { gamepad.layout = it }
        endEdit()
    }

    private fun endEdit() {
        editDialog = null
        gamepad.editMode = false
        sessionStartLayout = null
        undoStack.clear()
        setMode(Mode.EXPANDED)
    }

    private fun openControlDialog(id: String) {
        val spec = gamepad.layout.find(id) ?: return
        editDialog?.dismiss()
        val dialog = ControlEditDialog(context, spec, object : ControlEditDialog.Callbacks {
            override fun onLive(spec: ControlSpec) {
                gamepad.updateSpec(spec)
            }

            override fun onCommit() {
                commitChange()
            }

            override fun onCopy(spec: ControlSpec) {
                // Drop the copy slightly down-right of the original, on-screen.
                val copy = spec.copy(
                    id = GamepadLayout.newId(),
                    cx = (spec.cx + 0.05f).coerceAtMost(0.97f),
                    cy = (spec.cy + 0.08f).coerceAtMost(0.95f)
                )
                gamepad.layout = gamepad.layout.add(copy)
                commitChange()
            }

            override fun onDelete(spec: ControlSpec) {
                gamepad.layout = gamepad.layout.remove(spec.id)
                commitChange()
            }
        })
        editDialog = dialog
        dialog.show()
    }
}