package com.robertvokac.lexicon.share

import android.content.Intent
import com.robertvokac.lexicon.ui.markdown.SafeLinks

/** What a share proposes for a new item. Nothing is saved until the person taps Save. */
data class SharePrefill(val title: String, val content: String)

/** Why the app was opened, beyond simply launching it. */
sealed interface LaunchRequest {
    /** The Quick Add app shortcut. */
    data object QuickAdd : LaunchRequest

    /** Text or a URL shared from another app. */
    data class Share(val prefill: SharePrefill) : LaunchRequest
}

/**
 * Reads untrusted launch intents. Only the Quick Add shortcut and ACTION_SEND
 * with text/plain are understood; everything else is ignored. Shared text is
 * cleaned of control characters and bounded in size.
 */
object LaunchIntents {
    const val ACTION_QUICK_ADD = "com.robertvokac.lexicon.action.QUICK_ADD"
    const val MAX_CONTENT = 64_000
    const val MAX_TITLE = 200

    fun parse(intent: Intent?): LaunchRequest? {
        intent ?: return null
        return when (intent.action) {
            ACTION_QUICK_ADD -> LaunchRequest.QuickAdd
            Intent.ACTION_SEND -> parseShare(
                action = intent.action,
                type = intent.type,
                text = runCatching { intent.getCharSequenceExtra(Intent.EXTRA_TEXT) }.getOrNull()?.toString(),
                subject = runCatching { intent.getStringExtra(Intent.EXTRA_SUBJECT) }.getOrNull()
                    ?: runCatching { intent.getStringExtra(Intent.EXTRA_TITLE) }.getOrNull(),
            )?.let(LaunchRequest::Share)
            else -> null
        }
    }

    /**
     * A shared subject becomes the title. A shared URL becomes a Markdown
     * link in the content; other text keeps its first line as the title and
     * the whole text as content when there is more than one line.
     */
    fun parseShare(action: String?, type: String?, text: String?, subject: String?): SharePrefill? {
        if (action != Intent.ACTION_SEND) return null
        val mime = type?.substringBefore(';')?.trim()?.lowercase()
        if (mime != "text/plain") return null
        val body = clean(text.orEmpty(), keepNewlines = true).trim().take(MAX_CONTENT)
        val heading = clean(subject.orEmpty(), keepNewlines = false).trim().take(MAX_TITLE)
        if (body.isEmpty() && heading.isEmpty()) return null
        val isUrl = body.isNotEmpty() && !body.any { it.isWhitespace() } && SafeLinks.isAllowed(body) &&
            (body.startsWith("http://", ignoreCase = true) || body.startsWith("https://", ignoreCase = true))
        return when {
            isUrl -> SharePrefill(title = heading, content = "<$body>")
            heading.isNotEmpty() -> SharePrefill(title = heading, content = body)
            else -> {
                val lines = body.lines()
                val first = lines.first().trim().take(MAX_TITLE)
                SharePrefill(title = first, content = if (lines.size > 1 || first.length < lines.first().trim().length) body else "")
            }
        }
    }

    private fun clean(value: String, keepNewlines: Boolean): String = buildString(value.length) {
        for (ch in value) {
            when {
                ch == '\n' -> append(if (keepNewlines) '\n' else ' ')
                ch == '\t' -> append(if (keepNewlines) '\t' else ' ')
                ch == '\r' -> Unit
                ch.isISOControl() -> Unit
                else -> append(ch)
            }
        }
    }
}
