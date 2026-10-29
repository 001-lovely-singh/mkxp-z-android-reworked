package com.anyplay.engine

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File

/** The three checkbox sections of the cheat panel. */
enum class CheatSection(val title: String) {
    CHARACTER("Character states"),
    EXPLORATION("Exploration"),
    BATTLE("Battle")
}

/**
 * Every checkbox cheat. [wireName] MUST match the names used in
 * rb/anyplay_prelude.rb (module AnyPlayCheats) exactly.
 */
enum class CheatToggle(
    val wireName: String,
    val label: String,
    val section: CheatSection
) {
    // Character states
    UNLIMITED_HPMP("hpmp", "Unlimited HP/MP", CheatSection.CHARACTER),
    UNLIMITED_TP("tp", "Unlimited TP", CheatSection.CHARACTER),
    UNLIMITED_GOLD("gold", "Unlimited Gold", CheatSection.CHARACTER),
    STATUS_IMMUNITY("status", "Status ailment immunity", CheatSection.CHARACTER),
    UNLIMITED_ITEMS("items", "Unlimited Items", CheatSection.CHARACTER),

    // Exploration
    WALK_THROUGH_WALLS("noclip", "Walk through walls", CheatSection.EXPLORATION),
    NO_ENCOUNTERS("noEncounter", "No random encounters", CheatSection.EXPLORATION),
    SPEED_BOOST("speed", "Speed boost", CheatSection.EXPLORATION),
    FAST_TEXT("fastText", "Fast text speed", CheatSection.EXPLORATION),

    // Battle
    ONE_HIT_KILL("oneHit", "One-hit kill", CheatSection.BATTLE),
    INVINCIBILITY("invincible", "Invincibility", CheatSection.BATTLE),
    ALWAYS_CRIT("crit", "Always critical hit", CheatSection.BATTLE),
    ESCAPE_ALWAYS("escape", "Escape always works", CheatSection.BATTLE),
    FREE_SKILL_COST("freeSkill", "Free skill cost (MP/TP/PP)", CheatSection.BATTLE)
}

/**
 * CheatManager (RGSS plugin version)
 * ----------------------------------
 * Same public API as the AnyPlay app's CheatManager, but the cheats run
 * inside the game's Ruby code (AnyPlayCheats in anyplay_prelude.rb) instead
 * of JavaScript.
 *
 * Talking to the game: one command at a time is written to a small file in
 * the app cache folder; the game picks it up a few times per second, runs
 * it and writes the answer into a reply file. Commands wait in a queue so
 * quick taps never overwrite each other.
 *
 * Must be used from the main thread.
 */
class CheatManager(
    context: Context,
    gameKey: String
) {
    private val prefs =
        context.applicationContext.getSharedPreferences("anyplay_cheats", Context.MODE_PRIVATE)

    private val keyBase = "game_${gameKey.hashCode()}"
    private val togglesKey = "$keyBase.toggles"
    private val speedKey = "$keyBase.speed"

    private val dir = context.applicationContext.cacheDir
    private val cmdFile = File(dir, "anyplay_cheat_cmd.txt")
    private val tmpFile = File(dir, "anyplay_cheat_cmd.tmp")
    private val replyFile = File(dir, "anyplay_cheat_reply.txt")
    private val handler = Handler(Looper.getMainLooper())

    private class Pending(
        val id: Int,
        val line: String,
        val deadline: Long, // 0 = wait forever
        val callback: ((String) -> Unit)?
    ) {
        var sent = false
    }

    private val queue = ArrayDeque<Pending>()
    private var nextId = 1
    private var ready = false
    private var polling = false

    init {
        cmdFile.delete()
        tmpFile.delete()
        replyFile.delete()
        // Gives the game the saved checkboxes as soon as it starts running.
        enqueue("restore\t${speedMultiplier()}\t${enabledNames().joinToString(",")}", 0L) { ready = true }
    }

    private fun enabledNames(): Set<String> =
        prefs.getStringSet(togglesKey, emptySet())?.toSet() ?: emptySet()

    fun isEnabled(toggle: CheatToggle): Boolean = enabledNames().contains(toggle.wireName)

    fun speedMultiplier(): Float = prefs.getFloat(speedKey, 2f)

    /**
     * Checkbox tapped. [onResult] gets "" when it worked, otherwise a message
     * (for example "This cheat doesn't work in this type of game.") and the
     * checkbox must go back to OFF.
     */
    fun setEnabled(toggle: CheatToggle, enabled: Boolean, onResult: (String) -> Unit = {}) {
        send("toggle\t${toggle.wireName}\t${if (enabled) "1" else "0"}") { msg ->
            if (msg.isEmpty()) {
                val updated = enabledNames().toMutableSet()
                if (enabled) updated.add(toggle.wireName) else updated.remove(toggle.wireName)
                prefs.edit().putStringSet(togglesKey, updated).apply()
            } else if (!enabled) {
                // Turning OFF can never really fail; keep the saved state honest.
                val updated = enabledNames().toMutableSet().apply { remove(toggle.wireName) }
                prefs.edit().putStringSet(togglesKey, updated).apply()
            }
            onResult(msg)
        }
    }

    /**
     * "Set" button on a value row. [name] is gold, level, exp, atk, def, mat,
     * mdf, agi, luk, speed, item, weapon, armor. [b] is only used by
     * item/weapon/armor (quantity).
     */
    fun setValue(name: String, a: String, b: String = "", onResult: (String) -> Unit) {
        if (name == "item" || name == "weapon" || name == "armor") {
            send("give\t$name\t${clean(a)}\t${clean(b)}", onResult)
            return
        }
        send("value\t$name\t${clean(a)}") { msg ->
            if (name == "speed" && msg.startsWith("Speed x")) {
                a.trim().toFloatOrNull()?.takeIf { it > 0f }?.let {
                    prefs.edit().putFloat(speedKey, it.coerceIn(0.25f, 8f)).apply()
                    val updated = enabledNames().toMutableSet().apply { add(CheatToggle.SPEED_BOOST.wireName) }
                    prefs.edit().putStringSet(togglesKey, updated).apply()
                }
            }
            onResult(msg)
        }
    }

    /** One-click button (maxExp, instantWin, fullInventory). */
    fun runAction(name: String, onResult: (String) -> Unit) {
        send("run\t$name", onResult)
    }

    /**
     * Fetches [(id, name), ...] for "item" / "weapon" / "armor" from the
     * running game so the picker can show real names. Empty list if the game
     * isn't ready or has none of that kind.
     */
    fun listItems(kind: String, onResult: (List<Pair<Int, String>>) -> Unit) {
        send("list\t$kind") { raw ->
            val out = mutableListOf<Pair<Int, String>>()
            raw.lineSequence().forEach { line ->
                val p = line.split('\t', limit = 2)
                val id = p.getOrNull(0)?.trim()?.toIntOrNull()
                if (id != null && p.size == 2) out.add(id to p[1])
            }
            onResult(out)
        }
    }

    // ---- queue --------------------------------------------------------

    private fun clean(s: String) = s.trim().replace('\t', ' ').replace('\n', ' ').replace('\r', ' ')

    private fun send(line: String, onResult: ((String) -> Unit)?) {
        if (!ready) {
            onResult?.invoke("The game is still loading. Try again in a moment.")
            return
        }
        enqueue(line, 8000L, onResult)
    }

    private fun enqueue(line: String, timeoutMs: Long, onResult: ((String) -> Unit)?) {
        val deadline = if (timeoutMs == 0L) 0L else System.currentTimeMillis() + timeoutMs
        queue.addLast(Pending(nextId++, line, deadline, onResult))
        pump()
        startPolling()
    }

    /** Writes the first waiting command into the command file. */
    private fun pump() {
        val head = queue.firstOrNull() ?: return
        if (head.sent) return
        try {
            tmpFile.writeText("${head.id}\t${head.line}")
            tmpFile.renameTo(cmdFile)
            head.sent = true
        } catch (e: Exception) {
            queue.removeFirst()
            head.callback?.invoke("Couldn't talk to the game.")
            pump()
        }
    }

    private fun startPolling() {
        if (polling) return
        polling = true
        handler.postDelayed(pollRunnable, 80)
    }

    private val pollRunnable = object : Runnable {
        override fun run() {
            val head = queue.firstOrNull()
            if (head == null) {
                polling = false
                return
            }
            var answered = false
            try {
                if (replyFile.exists()) {
                    val text = replyFile.readText()
                    val parts = text.split('\t', limit = 2)
                    if (parts[0].trim() == head.id.toString()) {
                        replyFile.delete()
                        queue.removeFirst()
                        answered = true
                        head.callback?.invoke(parts.getOrElse(1) { "" })
                    } else if (parts[0].trim().toIntOrNull()?.let { it < head.id } == true) {
                        replyFile.delete() // old answer, nobody waits for it
                    }
                }
            } catch (e: Exception) {
                // File still being written; try again next round.
            }
            if (!answered && head.deadline != 0L && System.currentTimeMillis() > head.deadline) {
                queue.removeFirst()
                cmdFile.delete()
                head.callback?.invoke("The game didn't answer. Try again.")
                answered = true
            }
            if (answered) pump()
            handler.postDelayed(this, 80)
        }
    }

    fun shutdown() {
        handler.removeCallbacks(pollRunnable)
        polling = false
        queue.clear()
    }
}
