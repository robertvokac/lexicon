package com.robertvokac.lexicon.ui.item

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.rememberTransformableState
import androidx.compose.foundation.gestures.transformable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.model.ImageValues
import com.robertvokac.lexicon.ui.common.LocalAppContainer
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * The pictures of Image values. A stored file never changes, so a picture
 * decoded once serves every screen while memory allows.
 */
object StoredImages {
    private val cache = object : LruCache<String, ImageBitmap>(32 * 1024 * 1024) {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }

    /** The picture, at most about [maxEdge] pixels on its longer side. */
    suspend fun load(api: LexiconApi, value: String, maxEdge: Int): ImageBitmap {
        val key = "$maxEdge/$value"
        cache.get(key)?.let { return it }
        val image = ImageValues.parse(value) ?: throw IllegalArgumentException("This is not an image value.")
        val bytes = api.blobBytes(image.hash)
        val bitmap = withContext(Dispatchers.Default) { decode(bytes, maxEdge) }
            ?: throw IllegalArgumentException("This ${ImageValues.describe(value)} cannot be shown.")
        return bitmap.asImageBitmap().also { cache.put(key, it) }
    }

    /** Decodes a smaller copy of a large image: never more than twice [maxEdge]. */
    fun decode(bytes: ByteArray, maxEdge: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= maxEdge || bounds.outHeight / (sample * 2) >= maxEdge) sample *= 2
        return BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sample })
    }
}

/** An Image value's picture, loading and failing quietly in its own box. */
@Composable
fun StoredImage(
    imageValue: String,
    contentDescription: String,
    modifier: Modifier = Modifier,
    maxEdge: Int = 720,
    contentScale: ContentScale = ContentScale.Fit,
) {
    val api = LocalAppContainer.current.api
    val loaded by produceState<Result<ImageBitmap>?>(null, imageValue, maxEdge) {
        value = try {
            Result.success(StoredImages.load(api, imageValue, maxEdge))
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: ApiException) {
            Result.failure(failure)
        } catch (failure: IllegalArgumentException) {
            Result.failure(failure)
        }
    }
    val bitmap = loaded?.getOrNull()
    when {
        bitmap != null -> Image(bitmap, contentDescription, modifier, contentScale = contentScale)
        loaded == null -> Box(modifier, contentAlignment = Alignment.Center) { CircularProgressIndicator(Modifier.size(24.dp)) }
        else -> Box(modifier, contentAlignment = Alignment.Center) {
            Text(
                loaded?.exceptionOrNull()?.message ?: "The image cannot be shown.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(8.dp),
            )
        }
    }
}

/** The whole picture over the screen: pinch to zoom, drag to move. */
@Composable
fun ImageViewerDialog(imageValue: String, title: String, onDismiss: () -> Unit) {
    var scale by remember { mutableFloatStateOf(1f) }
    var offset by remember { mutableStateOf(Offset.Zero) }
    // Zooms about the middle; drag moves the enlarged picture.
    val transform = rememberTransformableState { _, zoom, pan, _ ->
        scale = (scale * zoom).coerceIn(1f, 8f)
        offset = if (scale == 1f) Offset.Zero else offset + pan
    }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.surface) {
            Column {
                Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(title, style = MaterialTheme.typography.titleLarge)
                        ImageValues.describe(imageValue)?.let {
                            Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    IconButton(onClick = onDismiss) { Icon(Icons.Filled.Close, contentDescription = "Close the image") }
                }
                Box(
                    Modifier
                        .fillMaxSize()
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                        .transformable(transform),
                    contentAlignment = Alignment.Center,
                ) {
                    StoredImage(
                        imageValue,
                        contentDescription = "$title, full size",
                        maxEdge = 2048,
                        modifier = Modifier
                            .fillMaxSize()
                            .graphicsLayer {
                                scaleX = scale
                                scaleY = scale
                                translationX = offset.x
                                translationY = offset.y
                            },
                    )
                }
            }
        }
    }
}
