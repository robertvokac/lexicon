package com.robertvokac.lexicon.model

/**
 * Image values: "<media type>:<SHA-256 of the file>", such as
 * "image/png:6c7d…". The server's counterpart is lexicon-core/ImageValue.h.
 */
object ImageValues {
    /** The image types Lexicon stores and shows. No SVG: it can carry script. */
    val mediaTypes = listOf("image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp")

    data class Parsed(val mediaType: String, val hash: String)

    private val pattern = Regex("(image/[a-z]+):([0-9a-f]{64})")
    private val names = mapOf("image/png" to "PNG", "image/jpeg" to "JPEG", "image/gif" to "GIF", "image/webp" to "WebP", "image/bmp" to "BMP")
    private val extensions = mapOf("image/png" to "png", "image/jpeg" to "jpg", "image/gif" to "gif", "image/webp" to "webp", "image/bmp" to "bmp")

    fun parse(value: String?): Parsed? {
        val match = pattern.matchEntire(value ?: return null) ?: return null
        val (mediaType, hash) = match.destructured
        return if (mediaType in mediaTypes) Parsed(mediaType, hash) else null
    }

    fun format(mediaType: String, hash: String): String = "$mediaType:$hash"

    /** The image type [head] begins with, or null. The first 16 bytes are enough. */
    fun sniff(head: ByteArray): String? {
        fun startsWith(prefix: ByteArray, at: Int = 0) =
            head.size >= at + prefix.size && prefix.indices.all { head[at + it] == prefix[it] }
        fun ascii(text: String) = text.toByteArray(Charsets.US_ASCII)
        return when {
            startsWith(byteArrayOf(0x89.toByte(), 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a)) -> "image/png"
            startsWith(byteArrayOf(0xff.toByte(), 0xd8.toByte(), 0xff.toByte())) -> "image/jpeg"
            startsWith(ascii("GIF87a")) || startsWith(ascii("GIF89a")) -> "image/gif"
            startsWith(ascii("RIFF")) && startsWith(ascii("WEBP"), 8) -> "image/webp"
            startsWith(ascii("BM")) && head.size >= 14 -> "image/bmp"
            else -> null
        }
    }

    /** "PNG image", or null for a value that is no image value. */
    fun describe(value: String?): String? = parse(value)?.let { "${names.getValue(it.mediaType)} image" }

    /** "<field name>.<extension>" for Save as. */
    fun fileName(fieldName: String, value: String): String {
        val base = fieldName.replace(Regex("[^A-Za-z0-9._ -]+"), "_").trim().ifEmpty { "image" }
        return parse(value)?.let { "$base.${extensions.getValue(it.mediaType)}" } ?: base
    }

    const val NOT_AN_IMAGE = "Choose a PNG, JPEG, GIF, WebP or BMP image."
}
