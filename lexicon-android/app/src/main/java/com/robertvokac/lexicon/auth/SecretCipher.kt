package com.robertvokac.lexicon.auth

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.GeneralSecurityException
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Authenticated encryption of small secrets such as the session token. */
interface SecretCipher {
    /** Encrypts [plaintext]; [associatedData] must be given again to decrypt. */
    fun encrypt(plaintext: ByteArray, associatedData: ByteArray): ByteArray

    /** Throws GeneralSecurityException when the data was altered or the key is gone. */
    fun decrypt(sealed: ByteArray, associatedData: ByteArray): ByteArray
}

/**
 * AES-256-GCM with a fresh random IV per message. The sealed format is one
 * version byte, the 12-byte IV and the ciphertext with its 128-bit tag.
 */
class AesGcmSecretCipher(private val key: () -> SecretKey) : SecretCipher {
    override fun encrypt(plaintext: ByteArray, associatedData: ByteArray): ByteArray {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        // The provider chooses the IV; Android Keystore insists on doing so.
        cipher.init(Cipher.ENCRYPT_MODE, key())
        cipher.updateAAD(associatedData)
        val iv = cipher.iv
        check(iv.size == IV_BYTES) { "Unexpected IV length ${iv.size}." }
        return byteArrayOf(FORMAT_VERSION) + iv + cipher.doFinal(plaintext)
    }

    override fun decrypt(sealed: ByteArray, associatedData: ByteArray): ByteArray {
        if (sealed.size <= 1 + IV_BYTES || sealed[0] != FORMAT_VERSION) {
            throw GeneralSecurityException("Unknown sealed secret format.")
        }
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(TAG_BITS, sealed, 1, IV_BYTES))
        cipher.updateAAD(associatedData)
        return cipher.doFinal(sealed, 1 + IV_BYTES, sealed.size - 1 - IV_BYTES)
    }

    private companion object {
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val FORMAT_VERSION: Byte = 1
        const val IV_BYTES = 12
        const val TAG_BITS = 128
    }
}

/**
 * The session key lives in Android Keystore: generated on this device,
 * never exportable, and not part of any backup. A token encrypted with it is
 * useless anywhere else.
 */
object KeystoreKeys {
    private const val PROVIDER = "AndroidKeyStore"
    private const val SESSION_KEY_ALIAS = "lexicon.session.v1"

    @Synchronized
    fun sessionKey(): SecretKey {
        val keyStore = KeyStore.getInstance(PROVIDER).apply { load(null) }
        (keyStore.getKey(SESSION_KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, PROVIDER)
        generator.init(
            KeyGenParameterSpec.Builder(
                SESSION_KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setRandomizedEncryptionRequired(true)
                .build(),
        )
        return generator.generateKey()
    }
}
