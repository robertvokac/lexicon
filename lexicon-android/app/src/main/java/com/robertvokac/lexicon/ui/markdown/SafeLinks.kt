package com.robertvokac.lexicon.ui.markdown

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.core.net.toUri
import java.net.URI

/**
 * Links in item content point outside the app. Only the schemes the web
 * client's sanitizer allows are ever followed, and only through a browsable
 * VIEW intent the system resolves: no javascript:, intent:, file:, content:
 * or app-internal destinations. A tel: link opens the dialer; it never calls.
 */
object SafeLinks {
    private val allowedSchemes = setOf("http", "https", "mailto", "tel")
    private val opaqueSchemes = setOf("mailto", "tel")

    fun isAllowed(destination: String): Boolean {
        val trimmed = destination.trim()
        if (trimmed.isEmpty() || trimmed.any { it.isISOControl() || it.isWhitespace() }) return false
        val uri = runCatching { URI(trimmed) }.getOrNull() ?: return false
        val scheme = uri.scheme?.lowercase() ?: return false
        if (scheme !in allowedSchemes) return false
        return scheme in opaqueSchemes || !uri.host.isNullOrEmpty()
    }

    fun open(context: Context, destination: String) {
        if (!isAllowed(destination)) return
        val intent = Intent(Intent.ACTION_VIEW, destination.trim().toUri())
            .addCategory(Intent.CATEGORY_BROWSABLE)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            context.startActivity(intent)
        } catch (_: ActivityNotFoundException) {
            Toast.makeText(context, "No app can open this link.", Toast.LENGTH_SHORT).show()
        }
    }
}
