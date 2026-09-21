package com.robertvokac.lexicon

import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.robertvokac.lexicon.share.LaunchIntents
import com.robertvokac.lexicon.ui.LaunchRequests
import com.robertvokac.lexicon.ui.LexiconRoot

/**
 * The single Activity. It receives the launcher, the Quick Add shortcut and
 * text shared from other apps; everything else is Compose navigation.
 */
class MainActivity : ComponentActivity() {
    private val launchRequests: LaunchRequests by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        // A recreated Activity has already taken its launch request.
        if (savedInstanceState == null) accept(intent)
        val container = (application as LexiconApplication).container
        setContent {
            LexiconRoot(
                container = container,
                launchRequests = launchRequests,
                onShareFinished = { saved ->
                    if (saved) Toast.makeText(this, getString(R.string.share_saved), Toast.LENGTH_SHORT).show()
                    // Back to the app that shared.
                    finish()
                },
                onExit = { if (!moveTaskToBack(true)) finish() },
                onDarkThemeChanged = { dark ->
                    enableEdgeToEdge(
                        statusBarStyle = if (dark) {
                            SystemBarStyle.dark(Color.TRANSPARENT)
                        } else {
                            SystemBarStyle.light(Color.TRANSPARENT, Color.TRANSPARENT)
                        },
                        navigationBarStyle = if (dark) {
                            SystemBarStyle.dark(Color.TRANSPARENT)
                        } else {
                            SystemBarStyle.light(Color.TRANSPARENT, Color.TRANSPARENT)
                        },
                    )
                },
            )
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        accept(intent)
    }

    private fun accept(intent: Intent?) {
        LaunchIntents.parse(intent)?.let { launchRequests.pending.value = it }
    }
}
