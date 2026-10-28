package com.anyplay.overlay

import android.app.Dialog
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.InputFilter
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.BaseAdapter
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * ControlEditDialog
 * -------------------
 * The settings sheet that opens when you tap a button (or the D-pad) in the
 * gamepad editor. Redesigned compared to the reference's plain
 * spinner-and-slider form:
 *
 *   - LIVE PREVIEW of the control at the left (drawn by the very same
 *     ControlView the game uses) plus Copy / Delete,
 *   - searchable KEY PICKER (A-Z, 0-9, F1-F12, arrows, Enter, Esc, ...),
 *   - editable LABEL (auto-follows the key name until you type your own),
 *   - SHAPE chips: Circle / Square / Rounded / Oval (Circle and Square keep
 *     width == height automatically),
 *   - Width / Height / Text-size sliders with live values,
 *   - Opacity, 10 COLOR PRESETS, R/G/B sliders and a HEX field,
 *   - "Tap to hold" (latch) switch for buttons that should stay pressed.
 *
 * Every change is applied to the real overlay immediately ([Callbacks.onLive]);
 * a finished gesture (slider released, chip tapped, dialog closed) calls
 * [Callbacks.onCommit], which the editor turns into ONE undo step — that is
 * what makes the undo counter in the toolbar behave like the reference.
 */
internal class ControlEditDialog(
    private val context: Context,
    initial: ControlSpec,
    private val callbacks: Callbacks
) {
    interface Callbacks {
        fun onLive(spec: ControlSpec)
        fun onCommit()
        fun onCopy(spec: ControlSpec)
        fun onDelete(spec: ControlSpec)
    }

    private val density = context.resources.displayMetrics.density
    private var cur: ControlSpec = initial
    private val isDpad = initial.type == ControlType.DPAD
    private val dialog = Dialog(context)

    private lateinit var preview: ControlView
    private lateinit var keyValue: TextView
    private var labelEdit: EditText? = null
    private var hexEdit: EditText? = null
    private var suppressHex = false

    private var widthRow: SliderRow? = null
    private var heightRow: SliderRow? = null
    private var redRow: SliderRow? = null
    private var greenRow: SliderRow? = null
    private var blueRow: SliderRow? = null

    private val shapeChips = LinkedHashMap<ControlShape, TextView>()
    private val swatches = ArrayList<Pair<Int, View>>()

    private enum class ColorSource { PRESET, RGB, HEX }

    // ---- Public API -------------------------------------------------------

    fun show() {
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE)
        dialog.setContentView(buildContent())
        dialog.setOnDismissListener { callbacks.onCommit() }

        val dm = context.resources.displayMetrics
        val w = min((dm.widthPixels * 0.88f).toInt(), dp(640f))
        val h = min((dm.heightPixels * 0.94f).toInt(), dp(372f))
        dialog.window?.let { window ->
            window.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            window.setLayout(w, h)
            window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        }
        dialog.show()
        dialog.window?.let { window ->
            val controller = WindowCompat.getInsetsController(window, window.decorView)
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    fun dismiss() {
        if (dialog.isShowing) dialog.dismiss()
    }

    // ---- State ------------------------------------------------------------

    private fun update(new: ControlSpec) {
        cur = new
        refreshPreview()
        callbacks.onLive(new)
    }

    private fun refreshPreview() {
        preview.spec = cur
        // Big controls are scaled down so they always fit the preview box.
        val s = min(1f, 124f / max(cur.widthDp, cur.heightDp))
        preview.scaleX = s
        preview.scaleY = s
    }

    // ---- Content ----------------------------------------------------------

    private fun buildContent(): View {
        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            background = rounded(BG, 16f)
            setPadding(dp(16f), dp(12f), dp(16f), dp(14f))
        }

        val header = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val title = TextView(context).apply {
            text = if (isDpad) "Edit D-pad" else "Edit button"
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 17f)
            typeface = Typeface.DEFAULT_BOLD
        }
        header.addView(title, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        header.addView(
            pill("Done", fill = ACCENT, textColor = Color.parseColor("#0B1620")) { dialog.dismiss() },
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        )
        card.addView(header)

        val body = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL }
        card.addView(
            body,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f).apply { topMargin = dp(10f) }
        )
        body.addView(buildLeftColumn(), LinearLayout.LayoutParams(dp(150f), ViewGroup.LayoutParams.MATCH_PARENT))

        val scroll = ScrollView(context).apply {
            isVerticalScrollBarEnabled = false
            addView(buildSettings())
        }
        body.addView(
            scroll,
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f).apply { leftMargin = dp(14f) }
        )
        return card
    }

    private fun buildLeftColumn(): View {
        val col = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }

        val box = FrameLayout(context).apply { background = rounded(PREVIEW_BG, 12f) }
        preview = ControlView(context, cur, null)
        box.addView(preview, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER
        ))
        col.addView(box, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(140f)))
        refreshPreview()

        col.addView(TextView(context).apply {
            text = "Live preview"
            setTextColor(MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            gravity = Gravity.CENTER
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            .apply { topMargin = dp(6f) })

        if (!isDpad) {
            col.addView(
                outlined("Copy button", ACCENT) {
                    callbacks.onCopy(cur)
                    dialog.dismiss()
                },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(38f)).apply { topMargin = dp(14f) }
            )
            col.addView(
                outlined("Delete button", DANGER) {
                    callbacks.onDelete(cur)
                    dialog.dismiss()
                },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(38f)).apply { topMargin = dp(8f) }
            )
        }
        return col
    }

    private fun buildSettings(): View {
        val col = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, 0, dp(4f), dp(6f))
        }

        if (!isDpad) {
            col.addView(sectionTitle("LABEL & KEY", first = true))
            col.addView(buildLabelField())
            col.addView(buildKeyField(), LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(8f) })

            col.addView(sectionTitle("SHAPE"))
            col.addView(buildShapeChips())
        }

        col.addView(sectionTitle("SIZE", first = isDpad))
        if (isDpad) {
            val row = slider("Size", 80, 200, cur.widthDp.roundToInt(), { "$it dp" }) { v ->
                update(cur.copy(widthDp = v.toFloat(), heightDp = v.toFloat()))
            }
            col.addView(row.view)
        } else {
            widthRow = slider("Width", 24, 140, cur.widthDp.roundToInt(), { "$it dp" }) { v ->
                if (cur.shape.locksRatio) {
                    update(cur.copy(widthDp = v.toFloat(), heightDp = v.toFloat()))
                    heightRow?.set(v)
                } else {
                    update(cur.copy(widthDp = v.toFloat()))
                }
            }
            heightRow = slider("Height", 24, 140, cur.heightDp.roundToInt(), { "$it dp" }) { v ->
                if (cur.shape.locksRatio) {
                    update(cur.copy(widthDp = v.toFloat(), heightDp = v.toFloat()))
                    widthRow?.set(v)
                } else {
                    update(cur.copy(heightDp = v.toFloat()))
                }
            }
            col.addView(widthRow!!.view)
            col.addView(heightRow!!.view)
            col.addView(
                slider("Text size", 6, 28, cur.textDp.roundToInt(), { "$it dp" }) { v ->
                    update(cur.copy(textDp = v.toFloat()))
                }.view
            )
        }

        col.addView(sectionTitle("APPEARANCE"))
        col.addView(
            slider("Opacity", 10, 100, (cur.opacity * 100f).roundToInt(), { "$it %" }) { v ->
                update(cur.copy(opacity = v / 100f))
            }.view
        )
        col.addView(buildColorSection())

        if (!isDpad) {
            col.addView(sectionTitle("BEHAVIOR"))
            col.addView(buildLatchRow())
        }
        return col
    }

    // ---- Label / key / shape ---------------------------------------------

    private fun buildLabelField(): View {
        val edit = EditText(context).apply {
            setText(cur.label)
            hint = "Text on the button"
            setHintTextColor(MUTED)
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
            filters = arrayOf<InputFilter>(InputFilter.LengthFilter(10))
            background = rounded(FIELD, 8f)
            setPadding(dp(12f), dp(8f), dp(12f), dp(8f))
            imeOptions = EditorInfo.IME_ACTION_DONE
            setOnEditorActionListener { v, actionId, _ ->
                if (actionId == EditorInfo.IME_ACTION_DONE) {
                    v.clearFocus()
                    val imm = context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
                    imm.hideSoftInputFromWindow(v.windowToken, 0)
                    true
                } else {
                    false
                }
            }
            setOnFocusChangeListener { _, hasFocus -> if (!hasFocus) callbacks.onCommit() }
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                override fun afterTextChanged(s: Editable?) {
                    val t = s?.toString().orEmpty()
                    if (t != cur.label) update(cur.copy(label = t))
                }
            })
        }
        labelEdit = edit
        return edit
    }

    private fun buildKeyField(): View {
        keyValue = TextView(context).apply {
            text = keyText()
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            gravity = Gravity.CENTER_VERTICAL
            background = rounded(FIELD, 8f)
            setPadding(dp(12f), dp(10f), dp(12f), dp(10f))
            setOnClickListener { showKeyPicker() }
        }
        return keyValue
    }

    private fun keyText() = "Key:  ${KeyCatalog.nameOf(cur.keyCode)}   \u25BE"

    private fun setKey(entry: KeyCatalog.Entry) {
        val followsKeyName = cur.label.isBlank() || cur.label.equals(KeyCatalog.nameOf(cur.keyCode), ignoreCase = true)
        val newLabel = if (followsKeyName) entry.name else cur.label
        update(cur.copy(keyCode = entry.keyCode, label = newLabel))
        keyValue.text = keyText()
        if (labelEdit?.text?.toString() != newLabel) labelEdit?.setText(newLabel)
        callbacks.onCommit()
    }

    private fun buildShapeChips(): View {
        val row = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL }
        for (shape in ControlShape.values()) {
            val chip = TextView(context).apply {
                text = shape.title
                gravity = Gravity.CENTER
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                setOnClickListener { selectShape(shape) }
            }
            shapeChips[shape] = chip
            row.addView(
                chip,
                LinearLayout.LayoutParams(0, dp(36f), 1f).apply { rightMargin = dp(6f) }
            )
        }
        refreshShapeChips()
        return row
    }

    private fun selectShape(shape: ControlShape) {
        var w = cur.widthDp
        var h = cur.heightDp
        if (shape.locksRatio) {
            val m = min(w, h)
            w = m
            h = m
        }
        update(cur.copy(shape = shape, widthDp = w, heightDp = h))
        widthRow?.set(w.roundToInt())
        heightRow?.set(h.roundToInt())
        refreshShapeChips()
        callbacks.onCommit()
    }

    private fun refreshShapeChips() {
        for ((shape, chip) in shapeChips) {
            val selected = shape == cur.shape
            chip.background = rounded(if (selected) ACCENT else FIELD, 8f)
            chip.setTextColor(if (selected) Color.parseColor("#0B1620") else TEXT)
            chip.typeface = if (selected) Typeface.DEFAULT_BOLD else Typeface.DEFAULT
        }
    }

    // ---- Color ------------------------------------------------------------

    private fun buildColorSection(): View {
        val col = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }

        col.addView(TextView(context).apply {
            text = "Color"
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
        }, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(8f) })

        val swatchRow = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL }
        for (rgb in PRESETS) {
            val sw = View(context).apply {
                setOnClickListener {
                    applyColor(rgb, ColorSource.PRESET)
                    callbacks.onCommit()
                }
            }
            swatches += rgb to sw
            swatchRow.addView(sw, LinearLayout.LayoutParams(dp(28f), dp(28f)).apply { rightMargin = dp(8f) })
        }
        col.addView(
            HorizontalScrollView(context).apply {
                isHorizontalScrollBarEnabled = false
                addView(swatchRow)
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(6f) }
        )

        val c = cur.color and 0xFFFFFF
        redRow = slider("Red", 0, 255, Color.red(c), { "$it" }, tint = Color.parseColor("#EF5350")) { v ->
            applyColor((cur.color and 0x00FFFF) or (v shl 16), ColorSource.RGB)
        }
        greenRow = slider("Green", 0, 255, Color.green(c), { "$it" }, tint = Color.parseColor("#66BB6A")) { v ->
            applyColor((cur.color and 0xFF00FF) or (v shl 8), ColorSource.RGB)
        }
        blueRow = slider("Blue", 0, 255, Color.blue(c), { "$it" }, tint = Color.parseColor("#42A5F5")) { v ->
            applyColor((cur.color and 0xFFFF00) or v, ColorSource.RGB)
        }
        col.addView(redRow!!.view)
        col.addView(greenRow!!.view)
        col.addView(blueRow!!.view)

        val hexRow = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        hexRow.addView(TextView(context).apply {
            text = "Hex  #"
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
        })
        val hex = EditText(context).apply {
            setText(String.format("%06X", c))
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS or
                    InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            filters = arrayOf(
                InputFilter { source, _, _, _, _, _ ->
                    if (source.all { ch -> ch.isDigit() || ch.lowercaseChar() in 'a'..'f' }) null else ""
                },
                InputFilter.LengthFilter(6)
            )
            background = rounded(FIELD, 8f)
            setPadding(dp(10f), dp(6f), dp(10f), dp(6f))
            imeOptions = EditorInfo.IME_ACTION_DONE
            setOnEditorActionListener { v, actionId, _ ->
                if (actionId == EditorInfo.IME_ACTION_DONE) {
                    v.clearFocus()
                    val imm = context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
                    imm.hideSoftInputFromWindow(v.windowToken, 0)
                    true
                } else {
                    false
                }
            }
            setOnFocusChangeListener { _, hasFocus -> if (!hasFocus) callbacks.onCommit() }
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                override fun afterTextChanged(s: Editable?) {
                    if (suppressHex) return
                    val t = s?.toString().orEmpty()
                    if (t.length == 6) t.toIntOrNull(16)?.let { applyColor(it, ColorSource.HEX) }
                }
            })
        }
        hexEdit = hex
        hexRow.addView(hex, LinearLayout.LayoutParams(dp(110f), ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            leftMargin = dp(6f)
        })
        col.addView(hexRow, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(8f) })

        refreshSwatches()
        return col
    }

    private fun applyColor(rgb: Int, source: ColorSource) {
        val color = rgb and 0xFFFFFF
        update(cur.copy(color = color))
        if (source != ColorSource.RGB) {
            redRow?.set(Color.red(color))
            greenRow?.set(Color.green(color))
            blueRow?.set(Color.blue(color))
        }
        if (source != ColorSource.HEX) {
            suppressHex = true
            hexEdit?.setText(String.format("%06X", color))
            suppressHex = false
        }
        refreshSwatches()
    }

    private fun refreshSwatches() {
        for ((rgb, view) in swatches) {
            val selected = rgb == (cur.color and 0xFFFFFF)
            view.background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(Color.rgb(Color.red(rgb), Color.green(rgb), Color.blue(rgb)))
                setStroke(
                    if (selected) dp(2f) else dp(1f),
                    if (selected) Color.WHITE else Color.parseColor("#55FFFFFF")
                )
            }
        }
    }

    // ---- Behavior ---------------------------------------------------------

    private fun buildLatchRow(): View {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val texts = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        texts.addView(TextView(context).apply {
            text = "Tap to hold"
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        })
        texts.addView(TextView(context).apply {
            text = "Tap once to press and keep held, tap again to release"
            setTextColor(MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        })
        row.addView(texts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        val toggle = android.widget.Switch(context).apply {
            isChecked = cur.latch
            setOnCheckedChangeListener { _, checked ->
                update(cur.copy(latch = checked))
                callbacks.onCommit()
            }
        }
        row.addView(toggle)
        return row
    }

    // ---- Key picker -------------------------------------------------------

    private fun showKeyPicker() {
        val picker = Dialog(context)
        picker.requestWindowFeature(Window.FEATURE_NO_TITLE)

        var shown: List<KeyCatalog.Entry> = KeyCatalog.entries

        val root = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            background = rounded(BG, 16f)
            setPadding(dp(14f), dp(12f), dp(14f), dp(12f))
        }
        root.addView(TextView(context).apply {
            text = "Choose key"
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            typeface = Typeface.DEFAULT_BOLD
        })
        val search = EditText(context).apply {
            hint = "Search (A, F5, ENTER, UP ...)"
            setHintTextColor(MUTED)
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            isSingleLine = true
            background = rounded(FIELD, 8f)
            setPadding(dp(12f), dp(8f), dp(12f), dp(8f))
        }
        root.addView(search, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(8f) })

        val list = ListView(context).apply {
            divider = null
            isVerticalScrollBarEnabled = false
        }
        val adapter = object : BaseAdapter() {
            override fun getCount() = shown.size
            override fun getItem(position: Int) = shown[position]
            override fun getItemId(position: Int) = position.toLong()
            override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
                val tv = (convertView as? TextView) ?: TextView(context).apply {
                    setTextColor(TEXT)
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
                    setPadding(dp(12f), dp(10f), dp(12f), dp(10f))
                }
                val entry = shown[position]
                tv.text = entry.name
                tv.setBackgroundColor(if (entry.keyCode == cur.keyCode) SELECTED_ROW else Color.TRANSPARENT)
                return tv
            }
        }
        list.adapter = adapter
        list.setOnItemClickListener { _, _, position, _ ->
            setKey(shown[position])
            picker.dismiss()
        }
        search.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                val q = s?.toString().orEmpty().trim()
                shown = if (q.isEmpty()) KeyCatalog.entries
                else KeyCatalog.entries.filter { it.name.contains(q, ignoreCase = true) }
                adapter.notifyDataSetChanged()
            }
        })
        root.addView(list, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f
        ).apply { topMargin = dp(8f) })

        picker.setContentView(root)
        val dm = context.resources.displayMetrics
        picker.window?.let { window ->
            window.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            window.setLayout(min((dm.widthPixels * 0.6f).toInt(), dp(380f)), min((dm.heightPixels * 0.9f).toInt(), dp(360f)))
            window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        }
        picker.show()
        picker.window?.let { window ->
            val controller = WindowCompat.getInsetsController(window, window.decorView)
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    // ---- Small view helpers ----------------------------------------------

    private class SliderRow(
        val view: View,
        private val bar: SeekBar,
        private val valueText: TextView,
        private val min: Int,
        private val format: (Int) -> String
    ) {
        fun set(value: Int) {
            bar.progress = (value - min).coerceIn(0, bar.max)
            valueText.text = format(value)
        }
    }

    private fun slider(
        title: String,
        min: Int,
        max: Int,
        value: Int,
        format: (Int) -> String,
        tint: Int = ACCENT,
        onChange: (Int) -> Unit
    ): SliderRow {
        val wrap = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val head = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL }
        head.addView(TextView(context).apply {
            text = title
            setTextColor(TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        val valueText = TextView(context).apply {
            setTextColor(MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
        }
        head.addView(valueText)
        wrap.addView(head, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(8f) })

        val bar = SeekBar(context).apply {
            this.max = max - min
            progress = (value - min).coerceIn(0, max - min)
            progressTintList = ColorStateList.valueOf(tint)
            thumbTintList = ColorStateList.valueOf(tint)
            progressBackgroundTintList = ColorStateList.valueOf(TRACK)
        }
        valueText.text = format(value)
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                val v = progress + min
                valueText.text = format(v)
                if (fromUser) onChange(v)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {}

            override fun onStopTrackingTouch(seekBar: SeekBar) {
                callbacks.onCommit()
            }
        })
        wrap.addView(bar, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ))
        return SliderRow(wrap, bar, valueText, min, format)
    }

    private fun sectionTitle(text: String, first: Boolean = false): View =
        TextView(context).apply {
            this.text = text
            setTextColor(ACCENT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.DEFAULT_BOLD
            letterSpacing = 0.1f
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = if (first) 0 else dp(16f); bottomMargin = dp(2f) }
        }

    private fun pill(text: String, fill: Int, textColor: Int, onClick: () -> Unit): View =
        TextView(context).apply {
            this.text = text
            setTextColor(textColor)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            background = rounded(fill, 18f)
            setPadding(dp(20f), dp(8f), dp(20f), dp(8f))
            setOnClickListener { onClick() }
        }

    private fun outlined(text: String, color: Int, onClick: () -> Unit): View =
        TextView(context).apply {
            this.text = text
            setTextColor(color)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            background = GradientDrawable().apply {
                setColor(Color.TRANSPARENT)
                cornerRadius = dpF(10f)
                setStroke(dp(1.5f), color)
            }
            setOnClickListener { onClick() }
        }

    private fun rounded(color: Int, radiusDp: Float): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = dpF(radiusDp)
        }

    private fun dp(v: Float): Int = (v * density).roundToInt()
    private fun dpF(v: Float): Float = v * density

    private companion object {
        val BG = Color.parseColor("#FF1C2127")
        val FIELD = Color.parseColor("#FF2A323A")
        val PREVIEW_BG = Color.parseColor("#FF0E1114")
        val TEXT = Color.parseColor("#FFE9EEF2")
        val MUTED = Color.parseColor("#FF8FA3B5")
        val ACCENT = Color.parseColor("#FF4FC3F7")
        val DANGER = Color.parseColor("#FFEF5350")
        val TRACK = Color.parseColor("#FF3A444E")
        val SELECTED_ROW = Color.parseColor("#33" + "4FC3F7")

        val PRESETS = listOf(
            0x444444, 0x222222, 0x000000, 0xFFFFFF, 0xE53935,
            0xFB8C00, 0xFDD835, 0x43A047, 0x1E88E5, 0x8E24AA
        )
    }
}