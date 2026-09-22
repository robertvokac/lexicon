package com.robertvokac.lexicon.testing

import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.util.zip.CRC32
import java.util.zip.DeflaterOutputStream

/** Real image files for tests, made without the Android graphics stack. */
object TestImages {
    /** A width × height RGB PNG: blue with a red diagonal. */
    fun png(width: Int, height: Int): ByteArray {
        val pixels = ByteArrayOutputStream()
        DeflaterOutputStream(pixels).use { out ->
            for (y in 0 until height) {
                out.write(0)
                for (x in 0 until width) {
                    val band = kotlin.math.abs(x - y) < maxOf(1, width / 10)
                    out.write(if (band) byteArrayOf(0xe0.toByte(), 0x1b, 0x24) else byteArrayOf(0x1a, 0x5f, 0xb4.toByte()))
                }
            }
        }
        val file = ByteArrayOutputStream()
        val data = DataOutputStream(file)
        data.write(byteArrayOf(0x89.toByte(), 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a))
        fun chunk(type: String, body: ByteArray) {
            data.writeInt(body.size)
            val typed = type.toByteArray(Charsets.US_ASCII) + body
            data.write(typed)
            data.writeInt(CRC32().apply { update(typed) }.value.toInt())
        }
        val header = ByteArrayOutputStream()
        DataOutputStream(header).apply {
            writeInt(width)
            writeInt(height)
            write(byteArrayOf(8, 2, 0, 0, 0))
        }
        chunk("IHDR", header.toByteArray())
        chunk("IDAT", pixels.toByteArray())
        chunk("IEND", ByteArray(0))
        return file.toByteArray()
    }
}
