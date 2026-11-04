package me.arnee.waddle

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Typeface
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import org.json.JSONArray
import org.json.JSONObject

/**
 * One conversation: the messages, and writing (sent at the next sync, right away).
 * "+" starts a poll or a game of Fire på rad; they show as cards.
 */
class ChatActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var messages: LinearLayout
    private lateinit var scroll: ScrollView
    private lateinit var input: EditText
    private var conv = 0
    /** A chess square tapped first ("e2"), per game ("sender:counter"). */
    private val chessFrom = mutableMapOf<String, String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        conv = intent.getIntExtra("conv", 0)
        val page = Ui.column(this)
        page.setBackgroundColor(Ui.PAPER)
        val header = Ui.column(this)
        page.addView(header)
        messages = Ui.column(this)
        messages.setPadding(Ui.dp(this, 12), Ui.dp(this, 8), Ui.dp(this, 12), Ui.dp(this, 8))
        scroll = ScrollView(this).apply { addView(messages) }
        page.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        val bottom = Ui.row(this)
        bottom.setPadding(Ui.dp(this, 12), Ui.dp(this, 8), Ui.dp(this, 12), Ui.dp(this, 12))
        bottom.addView(Ui.button(this, "+", filled = false) { plus() })
        input = EditText(this).apply { hint = "Skriv melding …" }
        bottom.addView(input, LinearLayout.LayoutParams(0, -2, 1f))
        bottom.addView(Ui.button(this, "Send") { send() })
        page.addView(bottom)
        setContentView(page)
        Mesh.run {
            val title = Mesh.title(conv)
            main.post {
                val bar = Ui.header(this, if (conv > 32) "$title  ›" else title) { finish() }
                // A group: its members and "Forlat gruppe".
                if (conv > 32) bar.setOnClickListener {
                    startActivity(android.content.Intent(this, GroupActivity::class.java).putExtra("conv", conv))
                }
                header.addView(bar)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun send() {
        val text = input.text.toString().trim()
        if (text.isEmpty()) return
        input.setText("")
        Mesh.run {
            val sent = Mesh.send(conv, text)
            main.post { refresh() }
            if (sent) Server.sync()
            main.post {
                if (!sent) input.setText(text)
                refresh()
            }
        }
    }

    /** Runs [work] on the mesh thread; what it sent goes to the Pi right away. */
    private fun act(work: () -> Boolean) {
        Mesh.run {
            val sent = work()
            main.post { refresh() }
            if (sent) Server.sync()
            main.post { refresh() }
        }
    }

    private fun plus() {
        AlertDialog.Builder(this)
            .setItems(arrayOf("Avstemning", "Fire på rad", "Sjakk", "Senk skipet")) { _, which ->
                when (which) {
                    0 -> newPoll()
                    1 -> act { Mesh.startGame(conv, "fourinrow") }
                    2 -> act { Mesh.startGame(conv, "chess") }
                    else -> act { Mesh.startGame(conv, "ships") }
                }
            }
            .show()
    }

    private fun newPoll() {
        val form = Ui.column(this)
        form.setPadding(Ui.dp(this, 20), Ui.dp(this, 8), Ui.dp(this, 20), 0)
        val question = EditText(this).apply { hint = "Hva vil du spørre om?" }
        form.addView(question)
        val options = listOf("Svar 1", "Svar 2", "Svar 3 (valgfritt)", "Svar 4 (valgfritt)").map { EditText(this).apply { hint = it } }
        options.forEach { form.addView(it) }
        AlertDialog.Builder(this)
            .setTitle("Avstemning")
            .setView(form)
            .setPositiveButton("Send") { _, _ ->
                val q = question.text.toString().trim()
                val answers = options.map { it.text.toString().trim() }.filter { it.isNotEmpty() }
                if (q.isNotEmpty() && answers.size >= 2) act { Mesh.startPoll(conv, q, answers.joinToString("\n")) }
            }
            .setNegativeButton("Avbryt", null)
            .show()
    }

    private fun refresh() {
        Mesh.run {
            Mesh.markRead(conv)
            val chat = JSONArray(Mesh.chat(conv))
            val writable = Mesh.canWrite(conv)
            main.post { show(chat, writable) }
        }
    }

    private fun show(chat: JSONArray, writable: Boolean) {
        messages.removeAllViews()
        input.isEnabled = writable
        if (!writable) input.hint = "Venter på nøkkel fra T5-en"
        val group = conv == 0 || conv > 32
        for (i in 0 until chat.length()) messages.addView(bubble(chat.getJSONObject(i), group))
        scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    private fun bubble(m: JSONObject, group: Boolean): LinearLayout {
        val mine = m.optBoolean("mine")
        val box = Ui.column(this)
        box.gravity = if (mine) Gravity.END else Gravity.START
        val head = (if (!mine && group) m.optString("from") + "  " else "") + Ui.time(m.optLong("time"))
        box.addView(Ui.text(this, head, 12f, color = Ui.DIM))
        when {
            m.has("poll") -> box.addView(pollCard(m), LinearLayout.LayoutParams(-1, -2))
            m.has("game") && m.getJSONObject("game").optString("kind") == "chess" ->
                box.addView(chessCard(m), LinearLayout.LayoutParams(-1, -2))
            m.has("game") && m.getJSONObject("game").optString("kind") == "ships" ->
                box.addView(shipsCard(m), LinearLayout.LayoutParams(-1, -2))
            m.has("game") -> box.addView(gameCard(m), LinearLayout.LayoutParams(-1, -2))
            else -> {
                val text = Ui.text(this, m.optString("text"), 16f, color = if (mine) Color.WHITE else Ui.INK)
                text.background = Ui.rounded(if (mine) Ui.INK else Color.WHITE, Ui.dp(this, 14).toFloat(), if (mine) 0 else Ui.dp(this, 2))
                text.setPadding(Ui.dp(this, 12), Ui.dp(this, 8), Ui.dp(this, 12), Ui.dp(this, 8))
                box.addView(text)
            }
        }
        val note = if (mine) m.optString("state") else if (m.optBoolean("warning")) "(!) en melding kan mangle" else ""
        if (note.isNotEmpty()) box.addView(Ui.text(this, note, 12f, color = Ui.DIM))
        box.setPadding(0, Ui.dp(this, 6), 0, Ui.dp(this, 6))
        return box
    }

    private fun card(): LinearLayout = Ui.column(this).apply {
        background = Ui.rounded(Color.WHITE, Ui.dp(this@ChatActivity, 14).toFloat(), Ui.dp(this@ChatActivity, 2))
        setPadding(Ui.dp(this@ChatActivity, 14), Ui.dp(this@ChatActivity, 10), Ui.dp(this@ChatActivity, 14), Ui.dp(this@ChatActivity, 12))
    }

    /** A poll: tap an answer to vote (or move my vote there). */
    private fun pollCard(m: JSONObject): LinearLayout {
        val poll = m.getJSONObject("poll")
        val sender = m.optInt("sender")
        val counter = m.optLong("counter")
        val box = card()
        box.addView(Ui.text(this, poll.optString("question"), 17f, bold = true))
        box.addView(Ui.text(this, "Avstemning", 12f, color = Ui.DIM))
        val options = poll.getJSONArray("options")
        for (i in 0 until options.length()) {
            val o = options.getJSONObject(i)
            val mine = o.optBoolean("mine")
            val names = o.optString("names")
            val label = o.optString("text") + "  ·  " + o.optInt("count") + if (names.isNotEmpty()) "\n$names" else ""
            val row = Ui.text(this, label, 15f, color = if (mine) Color.WHITE else Ui.INK)
            row.background = Ui.rounded(if (mine) Ui.INK else Color.WHITE, Ui.dp(this, 10).toFloat(), Ui.dp(this, 2))
            row.setPadding(Ui.dp(this, 12), Ui.dp(this, 8), Ui.dp(this, 12), Ui.dp(this, 8))
            if (!mine) row.setOnClickListener { act { Mesh.vote(conv, sender, counter, i) } }
            box.addView(row, LinearLayout.LayoutParams(-1, -2).apply { topMargin = Ui.dp(this@ChatActivity, 8) })
        }
        return box
    }

    /** Fire på rad: the board, and a button over each column when it is my move. */
    private fun gameCard(m: JSONObject): LinearLayout {
        val game = m.getJSONObject("game")
        val sender = m.optInt("sender")
        val counter = m.optLong("counter")
        val box = card()
        box.addView(Ui.text(this, "Fire på rad", 17f, bold = true))
        box.addView(Ui.text(this, game.optString("players"), 13f, color = Ui.DIM))
        box.addView(Ui.text(this, game.optString("status"), 15f, bold = true))
        val board = game.optString("board")
        val canMove = game.optBoolean("canMove")
        val cols = 7
        val grid = Ui.column(this)
        grid.setPadding(0, Ui.dp(this, 8), 0, 0)
        if (canMove) {
            val drops = Ui.row(this)
            for (c in 0 until cols) {
                val full = board.getOrNull(c) != '0'
                val drop = Ui.text(this, if (full) "" else "↓", 20f, bold = true)
                drop.gravity = Gravity.CENTER
                if (!full) drop.setOnClickListener { act { Mesh.move(conv, sender, counter, c.toString()) } }
                drops.addView(drop, LinearLayout.LayoutParams(0, Ui.dp(this, 36), 1f))
            }
            grid.addView(drops)
        }
        for (r in 0 until board.length / cols) {
            val line = Ui.row(this)
            for (c in 0 until cols) {
                val piece = when (board[r * cols + c]) { '1' -> "●"; '2' -> "◯"; else -> "·" }
                val cell = Ui.text(this, piece, 22f).apply { gravity = Gravity.CENTER; typeface = Typeface.MONOSPACE }
                line.addView(cell, LinearLayout.LayoutParams(0, Ui.dp(this, 34), 1f))
            }
            grid.addView(line)
        }
        box.addView(grid, LinearLayout.LayoutParams(-1, -2))
        return box
    }

    /** Sjakk: tap a piece, then where it goes; the C++ side refuses illegal moves. */
    private fun chessCard(m: JSONObject): LinearLayout {
        val game = m.getJSONObject("game")
        val sender = m.optInt("sender")
        val counter = m.optLong("counter")
        val key = "$sender:$counter"
        val box = card()
        box.addView(Ui.text(this, "Sjakk", 17f, bold = true))
        box.addView(Ui.text(this, game.optString("players"), 13f, color = Ui.DIM))
        box.addView(Ui.text(this, game.optString("status"), 15f, bold = true))
        val board = game.optString("board")
        val flipped = game.optBoolean("flipped")
        val canMove = game.optBoolean("canMove")
        val from = chessFrom[key]
        val grid = Ui.column(this)
        grid.setPadding(0, Ui.dp(this, 8), 0, 0)
        for (row in 0 until 8) {
            val line = Ui.row(this)
            for (col in 0 until 8) {
                val file = if (flipped) 7 - col else col
                val rank = if (flipped) row else 7 - row
                val name = "${'a' + file}${rank + 1}"
                val piece = board.getOrNull(row * 8 + col) ?: '.'
                val glyph = when (piece) {
                    'K' -> "♔"; 'Q' -> "♕"; 'R' -> "♖"; 'B' -> "♗"; 'N' -> "♘"; 'P' -> "♙"
                    'k' -> "♚"; 'q' -> "♛"; 'r' -> "♜"; 'b' -> "♝"; 'n' -> "♞"; 'p' -> "♟"
                    else -> ""
                }
                val cell = Ui.text(this, glyph, 24f).apply { gravity = Gravity.CENTER }
                val dark = (file + rank) % 2 == 0
                cell.setBackgroundColor(if (name == from) Ui.INK else if (dark) Ui.LINE else Color.WHITE)
                if (name == from) cell.setTextColor(Color.WHITE)
                if (canMove) cell.setOnClickListener {
                    val first = chessFrom[key]
                    if (first == null || first == name) {
                        if (first == name) chessFrom.remove(key) else chessFrom[key] = name
                        refresh()
                    } else {
                        chessFrom.remove(key)
                        act { Mesh.move(conv, sender, counter, first + name) }
                    }
                }
                line.addView(cell, LinearLayout.LayoutParams(0, Ui.dp(this, 40), 1f))
            }
            grid.addView(line)
        }
        box.addView(grid, LinearLayout.LayoutParams(-1, -2))
        if (game.optBoolean("mine") && !game.optBoolean("over")) {
            box.addView(Ui.button(this, "Gi opp", filled = false) {
                AlertDialog.Builder(this)
                    .setTitle("Gi opp partiet?")
                    .setPositiveButton("Gi opp") { _, _ -> act { Mesh.move(conv, sender, counter, "x") } }
                    .setNegativeButton("Avbryt", null)
                    .show()
            })
        }
        return box
    }

    /** Senk skipet: tap a square on their sea to fire; my own sea is below. */
    private fun shipsCard(m: JSONObject): LinearLayout {
        val game = m.getJSONObject("game")
        val sender = m.optInt("sender")
        val counter = m.optLong("counter")
        val box = card()
        box.addView(Ui.text(this, "Senk skipet", 17f, bold = true))
        box.addView(Ui.text(this, game.optString("players"), 13f, color = Ui.DIM))
        box.addView(Ui.text(this, game.optString("status"), 15f, bold = true))
        val canMove = game.optBoolean("canMove")
        fun sea(cells: String, size: Int, fire: Boolean): LinearLayout {
            val grid = Ui.column(this)
            grid.setPadding(0, Ui.dp(this, 8), 0, 0)
            for (row in 0 until 8) {
                val line = Ui.row(this)
                for (col in 0 until 8) {
                    val c = cells.getOrNull(row * 8 + col) ?: '.'
                    val glyph = when (c) { 'm' -> "·"; 'h', 'x' -> "✕"; 's' -> "■"; '?' -> "○"; else -> "" }
                    val cell = Ui.text(this, glyph, if (size > 30) 20f else 12f).apply { gravity = Gravity.CENTER }
                    cell.background = Ui.rounded(if (c == 'o' || c == 'x') Ui.LINE else Color.WHITE, 0f, 1, Ui.LINE)
                    if (fire && canMove && c == '.') {
                        val name = "${'a' + col}${8 - row}"
                        cell.setOnClickListener { act { Mesh.move(conv, sender, counter, name) } }
                    }
                    line.addView(cell, LinearLayout.LayoutParams(Ui.dp(this, size), Ui.dp(this, size)))
                }
                grid.addView(line)
            }
            return grid
        }
        box.addView(sea(game.optString("board"), 38, true))
        box.addView(Ui.text(this, "Ditt hav", 13f, bold = true).apply { setPadding(0, Ui.dp(this@ChatActivity, 10), 0, 0) })
        box.addView(sea(game.optString("sea"), 20, false))
        if (game.optBoolean("mine") && !game.optBoolean("over")) {
            box.addView(Ui.button(this, "Gi opp", filled = false) {
                AlertDialog.Builder(this)
                    .setTitle("Gi opp partiet?")
                    .setPositiveButton("Gi opp") { _, _ -> act { Mesh.move(conv, sender, counter, "x") } }
                    .setNegativeButton("Avbryt", null)
                    .show()
            })
        }
        return box
    }
}
