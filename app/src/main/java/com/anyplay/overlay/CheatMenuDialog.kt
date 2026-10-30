package com.anyplay.overlay

import android.app.Activity
import android.content.Context
import android.view.KeyEvent
import android.view.inputmethod.InputMethodManager
import android.widget.AbsListView
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import android.widget.CheckBox
import android.widget.FrameLayout
import androidx.core.widget.CompoundButtonCompat
import android.content.res.ColorStateList
import com.anyplay.engine.CheatManager
import com.anyplay.engine.CheatSection
import com.anyplay.engine.CheatToggle

/**
 * CheatMenuDialog
 * ----------------
 * The cheat panel, built entirely in code (no XML), as a scrollable list:
 *
 *   Character states / Exploration / Battle
 *       label ........................ [checkbox]
 *   Value enter
 *       label ............ [input] [Set]
 *   One-click
 *       label ........................ [Do]
 *
 * Checkbox state lives in [CheatManager] (saved per game); this class
 * only draws it and forwards taps.
 */
class CheatMenuDialog(
    private val activity: Activity,
    private val host: ViewGroup,
    private val manager: CheatManager
) {
    /** The panel (and the item picker on top of it) live INSIDE the game window
     *  instead of a Dialog: a Dialog takes window focus and Android then pauses
     *  the game, so the cheats could not run while the menu is open. */
    private var panelView: View? = null
    private var pickerView: View? = null
    val isShowing: Boolean get() = panelView != null

    /** Back button: closes the picker first, then the panel. Returns true if it did something. */
    fun handleBack(): Boolean {
        if (pickerView != null) { closePicker(); return true }
        if (panelView != null) { dismiss(); return true }
        return false
    }

    fun dismiss() {
        closePicker()
        panelView?.let { host.removeView(it) }
        panelView = null
        activity.window.setSoftInputMode(oldInputMode)
    }

    private var oldInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_UNSPECIFIED

    private fun closePicker() {
        pickerView?.let {
            hideKeyboard(it)
            host.removeView(it)
        }
        pickerView = null
    }

    private fun hideKeyboard(v: View) {
        val imm = activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        imm.hideSoftInputFromWindow(v.windowToken, 0)
    }

    /** Full-screen dim layer with the card centered; tapping outside the card calls [onOutside]. */
    private fun scrimWith(card: View, w: Int, h: Int, onOutside: () -> Unit): FrameLayout {
        val scrim = FrameLayout(activity)
        scrim.setBackgroundColor(Color.argb(120, 0, 0, 0))
        scrim.isClickable = true
        scrim.setOnClickListener { onOutside() }
        card.isClickable = true
        scrim.addView(card, FrameLayout.LayoutParams(w, h, Gravity.CENTER))
        return scrim
    }

    private val dp = activity.resources.displayMetrics.density
    private fun px(v: Int) = (v * dp).toInt()

    private val bgColor = Color.rgb(32, 35, 41)
    private val accent = Color.rgb(120, 180, 255)
    private val textColor = Color.WHITE
    private val hintColor = Color.rgb(140, 145, 155)

    /** Kept so tapping "Set" on the speed row can also tick the Speed boost box. */
    private var speedCheckBox: CheckBox? = null
    private var silent = false

    fun show() {
        if (panelView != null) return
        val content = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(16), px(4), px(16), px(16))
        }

        // ---- Checkbox sections ----
        CheatSection.values().forEach { section ->
            content.addView(sectionHeader(section.title))
            CheatToggle.values().filter { it.section == section }.forEach { toggle ->
                content.addView(toggleRow(toggle))
            }
        }

        // ---- Value enter ----
        content.addView(sectionHeader("Value enter"))
        content.addView(valueRow("Set Gold", "amount", "gold"))
        content.addView(valueRow("Set Level", "level", "level"))
        content.addView(valueRow("Set EXP", "exp", "exp"))
        content.addView(valueRow("Set Attack", "value", "atk"))
        content.addView(valueRow("Set Defense", "value", "def"))
        content.addView(valueRow("Set Magic Attack", "value", "mat"))
        content.addView(valueRow("Set Magic Defense", "value", "mdf"))
        content.addView(valueRow("Set Agility", "value", "agi"))
        content.addView(valueRow("Set Luck", "value", "luk"))
        content.addView(
            valueRow(
                "Speed multiplier", "x", "speed", decimal = true,
                initial = manager.speedMultiplier().toString()
            )
        )
        content.addView(giveRow())

        // ---- One-click ----
        content.addView(sectionHeader("One-click"))
        content.addView(actionRow("Max EXP", "maxExp"))
        content.addView(actionRow("Instant battle win", "instantWin"))
        content.addView(actionRow("Full inventory", "fullInventory"))

        val scroll = ScrollView(activity).apply { addView(content) }

        // Fixed title bar with Close, so it stays visible while scrolling.
        val titleBar = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(px(16), px(10), px(12), px(6))
        }
        titleBar.addView(
            TextView(activity).apply {
                text = "Cheats"
                setTextColor(textColor)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
                typeface = Typeface.DEFAULT_BOLD
            },
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        )

        val root = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        root.addView(titleBar)
        root.addView(
            scroll,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        )

        titleBar.addView(pillButton("Close") { dismiss() })

        val dm = activity.resources.displayMetrics
        root.background = GradientDrawable().apply {
            setColor(bgColor)
            cornerRadius = px(12).toFloat()
        }
        // Wide and tall enough for a landscape game screen; the list scrolls.
        val w = (dm.widthPixels * 0.75f).toInt().coerceAtMost(px(560))
        val h = (dm.heightPixels * 0.9f).toInt()
        val scrim = scrimWith(root, w, h) { dismiss() }

        oldInputMode = activity.window.attributes.softInputMode
        // PAN, not RESIZE: resizing the window would resize the game surface.
        activity.window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN)
        host.addView(scrim, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        panelView = scrim
    }

    // ---- Row builders ---------------------------------------------------

    private fun sectionHeader(title: String) = TextView(activity).apply {
        text = title
        setTextColor(accent)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        typeface = Typeface.DEFAULT_BOLD
        setPadding(0, px(16), 0, px(4))
    }

    private fun label(text: String) = TextView(activity).apply {
        this.text = text
        setTextColor(textColor)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
    }

    private fun rowContainer() = LinearLayout(activity).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(0, px(6), 0, px(6))
    }

    /** "Unlimited Gold ............ [checkbox]" */
    private fun toggleRow(toggle: CheatToggle): View {
        val checkBox = CheckBox(activity).apply {
            CompoundButtonCompat.setButtonTintList(this, ColorStateList.valueOf(Color.WHITE))
            isChecked = manager.isEnabled(toggle)
            setOnCheckedChangeListener { btn, checked ->
                if (!silent) {
                    manager.setEnabled(toggle, checked) { msg ->
                        if (msg.isNotEmpty()) {
                            // Didn't work: put the box back and say why.
                            if (btn.isChecked != manager.isEnabled(toggle)) {
                                silent = true
                                btn.isChecked = manager.isEnabled(toggle)
                                silent = false
                            }
                            toast(msg)
                        }
                    }
                }
            }
        }
        if (toggle == CheatToggle.SPEED_BOOST) speedCheckBox = checkBox

        return rowContainer().apply {
            addView(label(toggle.label), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(checkBox)
            // Whole row is tappable, not just the small box.
            setOnClickListener { checkBox.toggle() }
        }
    }

    /** "Set Gold ........ [input] [Set]" */
    private fun valueRow(
        title: String,
        hint: String,
        cheatName: String,
        decimal: Boolean = false,
        initial: String = ""
    ): View {
        val input = numberInput(hint, decimal).apply { setText(initial) }
        val set = pillButton("Set") {
            manager.setValue(cheatName, input.text.toString()) { msg ->
                toast(msg)
                if (cheatName == "speed" && msg.startsWith("Speed x")) {
                    speedCheckBox?.let { if (!it.isChecked) it.isChecked = true }
                }
            }
        }
        return rowContainer().apply {
            addView(label(title), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(input, LinearLayout.LayoutParams(px(96), ViewGroup.LayoutParams.WRAP_CONTENT))
            addView(spacer())
            addView(set)
        }
    }

    /**
     * "Give": Pick opens one searchable list with everything this game can
     * give (items, weapons, armor and, in Pokemon games, Pokemon). The amount
     * box is the quantity, or the level when a Pokemon is picked.
     */
    private fun giveRow(): View {
        var selKind = ""
        var selKey = ""
        val selected = TextView(activity).apply {
            text = "Nothing picked yet"
            setTextColor(hintColor)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            maxLines = 2
        }
        val amount = numberInput("Qty / Lv", false)
        val pick = pillButton("Pick") {
            showItemPicker { p ->
                selKind = p.kind
                selKey = p.key
                selected.text = "${kindTitle(p.kind)}: ${p.name}"
                selected.setTextColor(textColor)
                if (p.kind == "pokemon") toast("Type the level in the amount box, then Give.")
            }
        }
        val give = pillButton("Give") {
            if (selKey.isEmpty()) toast("Pick something first (use Pick).")
            else manager.give(selKind, selKey, amount.text.toString()) { msg -> toast(msg) }
        }
        val top = rowContainer().apply {
            addView(label("Give item / Pokémon"), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(pick)
        }
        val bottom = rowContainer().apply {
            addView(selected, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(amount, LinearLayout.LayoutParams(px(84), ViewGroup.LayoutParams.WRAP_CONTENT))
            addView(spacer())
            addView(give)
        }
        return LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            addView(top)
            addView(bottom)
        }
    }

    private fun kindTitle(kind: String) = when (kind) {
        "item" -> "Item"
        "weapon" -> "Weapon"
        "armor" -> "Armor"
        "pokemon" -> "Pokémon"
        else -> kind
    }

    /**
     * Fetches the real item/weapon/armor names from the running game and
     * shows them in a searchable list. Tapping one calls [onPick] with
     * its (id, name) and closes the picker — [onPick] is where the
     * caller fills the ID field, so the player never has to know it.
     */
    private fun showItemPicker(onPick: (CheatManager.Pickable) -> Unit) {
        val loadingToast = Toast.makeText(activity, "Loading items…", Toast.LENGTH_SHORT)
        loadingToast.show()

        manager.listPickables { items ->
            loadingToast.cancel()
            if (items.isEmpty()) {
                toast("This kind of item doesn't exist in this game.")
                return@listPickables
            }

            // "#12  Potion" — the id prefix keeps entries unique even if
            // two items share a name, and lets the player see the ID too.
            val entries = items.map {
                val shown = if (it.key.toIntOrNull() != null) "#${it.key}  ${it.name}" else "${it.name}  (${it.key})"
                "[${kindTitle(it.kind)}] $shown"
            }
            val adapter = object : ArrayAdapter<String>(activity, android.R.layout.simple_list_item_1, entries.toMutableList()) {
                override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
                    val view = super.getView(position, convertView, parent) as TextView
                    view.setTextColor(textColor)
                    view.setBackgroundColor(bgColor)
                    view.setPadding(px(12), px(10), px(12), px(10))
                    return view
                }
            }

            val listView = ListView(activity).apply { this.adapter = adapter }

            val search = EditText(activity).apply {
                hint = "Search by name…"
                setHintTextColor(hintColor)
                setTextColor(textColor)
                setSingleLine(true)
                imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_ACTION_DONE
                // Done / Enter on the keyboard closes it (the list stays filtered).
                setOnEditorActionListener { v, actionId, event ->
                    val enterPressed = event?.keyCode == KeyEvent.KEYCODE_ENTER &&
                            event.action == KeyEvent.ACTION_DOWN
                    if (actionId == EditorInfo.IME_ACTION_DONE || enterPressed) {
                        hideKeyboard(v)
                        clearFocus()
                        true
                    } else false
                }
                setPadding(px(10), px(8), px(10), px(8))
                background = GradientDrawable().apply {
                    setColor(Color.rgb(48, 52, 60))
                    cornerRadius = px(6).toFloat()
                }
                addTextChangedListener(object : TextWatcher {
                    override fun afterTextChanged(s: Editable?) { adapter.filter.filter(s) }
                    override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                    override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                })
            }

            // No Cancel button / button bar: the list takes all the space.
            // Dismiss with a tap outside the dialog or the Back button.
            val container = LinearLayout(activity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(px(12), px(12), px(12), px(8))
                addView(
                    TextView(activity).apply {
                        text = "Select"
                        setTextColor(textColor)
                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
                        typeface = Typeface.DEFAULT_BOLD
                        setPadding(px(4), 0, 0, px(8))
                    },
                    LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
                )
                addView(search, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
                addView(listView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
            }

            // Scrolling the list also closes the keyboard so more items are visible.
            listView.setOnScrollListener(object : AbsListView.OnScrollListener {
                override fun onScrollStateChanged(view: AbsListView?, scrollState: Int) {
                    if (scrollState == AbsListView.OnScrollListener.SCROLL_STATE_TOUCH_SCROLL) {
                        hideKeyboard(search)
                    }
                }
                override fun onScroll(view: AbsListView?, first: Int, visible: Int, total: Int) {}
            })

            listView.setOnItemClickListener { _, _, pos, _ ->
                hideKeyboard(search)
                // Look up the ORIGINAL (id, name) by the text shown — not
                // by `pos`, since filtering changes which index is which.
                val shown = adapter.getItem(pos)
                val original = entries.indexOf(shown)
                if (original >= 0) onPick(items[original])
                closePicker()
            }

            container.background = GradientDrawable().apply {
                setColor(bgColor)
                cornerRadius = px(12).toFloat()
            }
            val dm = activity.resources.displayMetrics
            closePicker()
            val scrim = scrimWith(
                container,
                (dm.widthPixels * 0.75f).toInt().coerceAtMost(px(560)),
                (dm.heightPixels * 0.9f).toInt()
            ) { closePicker() }
            host.addView(scrim, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
            pickerView = scrim
        }
    }

    /** "Max EXP ............... [Do]" */
    private fun actionRow(title: String, actionName: String): View {
        val go = pillButton("Do") { manager.runAction(actionName) { msg -> toast(msg) } }
        return rowContainer().apply {
            addView(label(title), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(go)
        }
    }

    // ---- Small widgets --------------------------------------------------

    private fun numberInput(hint: String, decimal: Boolean) = EditText(activity).apply {
        this.hint = hint
        setHintTextColor(hintColor)
        setTextColor(textColor)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        inputType = InputType.TYPE_CLASS_NUMBER or
                if (decimal) InputType.TYPE_NUMBER_FLAG_DECIMAL else 0
        imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_ACTION_DONE
        setSingleLine(true)
        setPadding(px(8), px(6), px(8), px(6))
        background = GradientDrawable().apply {
            setColor(Color.rgb(48, 52, 60))
            cornerRadius = px(6).toFloat()
        }
    }

    private fun pillButton(text: String, onClick: () -> Unit) = TextView(activity).apply {
        this.text = text
        setTextColor(Color.WHITE)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        typeface = Typeface.DEFAULT_BOLD
        gravity = Gravity.CENTER
        setPadding(px(14), px(8), px(14), px(8))
        background = GradientDrawable().apply {
            setColor(Color.rgb(60, 120, 220))
            cornerRadius = px(8).toFloat()
        }
        isClickable = true
        setOnClickListener { onClick() }
    }

    private fun spacer() = View(activity).apply {
        layoutParams = LinearLayout.LayoutParams(px(8), 1)
    }

    private fun toast(msg: String) {
        if (msg.isNotBlank()) Toast.makeText(activity, msg, Toast.LENGTH_SHORT).show()
    }
}
