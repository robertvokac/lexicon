package com.robertvokac.lexicon.model

import com.robertvokac.lexicon.testing.TestImages
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ImageValuesTest {
    private val hash = "a".repeat(64)

    private fun bytes(vararg parts: Any): ByteArray = parts.flatMap { part ->
        when (part) {
            is String -> part.toByteArray(Charsets.ISO_8859_1).toList()
            is IntArray -> part.map { it.toByte() }
            else -> error("bytes or text")
        }
    }.toByteArray()

    @Test
    fun anImageIsKnownByItsFirstBytes() {
        assertEquals("image/png", ImageValues.sniff(TestImages.png(2, 2)))
        assertEquals("image/jpeg", ImageValues.sniff(bytes(intArrayOf(0xff, 0xd8, 0xff, 0xe0))))
        assertEquals("image/gif", ImageValues.sniff(bytes("GIF89a", intArrayOf(1, 0))))
        assertEquals("image/webp", ImageValues.sniff(bytes("RIFF", intArrayOf(0x24, 0, 0, 0), "WEBPVP8 ")))
        assertNull(ImageValues.sniff(bytes("RIFF", intArrayOf(0x24, 0, 0, 0), "WAVEfmt ")))
        assertEquals("image/bmp", ImageValues.sniff(bytes("BM", IntArray(12))))
        assertNull(ImageValues.sniff(bytes("<svg xmlns=\"http://www.w3.org/2000/svg\">")))
        assertNull(ImageValues.sniff(ByteArray(0)))
    }

    @Test
    fun aValueNamesItsTypeAndItsFile() {
        assertEquals(ImageValues.Parsed("image/png", hash), ImageValues.parse("image/png:$hash"))
        assertEquals("image/gif:$hash", ImageValues.format("image/gif", hash))
        assertNull(ImageValues.parse(hash))
        assertNull(ImageValues.parse("image/svg+xml:$hash"))
        assertNull(ImageValues.parse("image/png:${"A".repeat(64)}"))
        assertNull(ImageValues.parse(null))
    }

    @Test
    fun anImageIsDescribedAndNamedForSaving() {
        assertEquals("JPEG image", ImageValues.describe("image/jpeg:$hash"))
        assertNull(ImageValues.describe("text"))
        assertEquals("Diagram_ v2.jpg", ImageValues.fileName("Diagram: v2", "image/jpeg:$hash"))
        assertEquals("image.webp", ImageValues.fileName("", "image/webp:$hash"))
    }
}
