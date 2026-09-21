package com.robertvokac.lexicon.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import com.robertvokac.lexicon.storage.ThemePreference

// The palette of the desktop and web clients: neutral gray surfaces, a blue
// accent and selection. Blues are darkened or lightened where the desktop
// value would miss WCAG contrast for text on these surfaces.

private val LightColors = lightColorScheme(
    primary = Color(0xFF1A5FB4),
    onPrimary = Color.White,
    primaryContainer = Color(0xFFD3E4F9),
    onPrimaryContainer = Color(0xFF0B2E57),
    secondary = Color(0xFF4D5B6B),
    onSecondary = Color.White,
    secondaryContainer = Color(0xFFE2E6EB),
    onSecondaryContainer = Color(0xFF1C2630),
    tertiary = Color(0xFF6B4E8B),
    onTertiary = Color.White,
    background = Color(0xFFF4F4F4),
    onBackground = Color(0xFF1C1C1C),
    surface = Color(0xFFF4F4F4),
    onSurface = Color(0xFF1C1C1C),
    surfaceVariant = Color(0xFFE4E4E4),
    onSurfaceVariant = Color(0xFF4A4A4A),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFFAFAFA),
    surfaceContainer = Color(0xFFEFEFEF),
    surfaceContainerHigh = Color(0xFFE8E8E8),
    surfaceContainerHighest = Color(0xFFE0E0E0),
    outline = Color(0xFF8A8A8A),
    outlineVariant = Color(0xFFC3C3C3),
    error = Color(0xFFB3261E),
    onError = Color.White,
    errorContainer = Color(0xFFF9DEDC),
    onErrorContainer = Color(0xFF410E0B),
)

private val DarkColors = darkColorScheme(
    primary = Color(0xFF64B5FF),
    onPrimary = Color(0xFF002F55),
    primaryContainer = Color(0xFF1E5C99),
    onPrimaryContainer = Color(0xFFD6E8FF),
    secondary = Color(0xFFB8C4D2),
    onSecondary = Color(0xFF22303D),
    secondaryContainer = Color(0xFF3A4652),
    onSecondaryContainer = Color(0xFFDDE5EE),
    tertiary = Color(0xFFD2B8F0),
    onTertiary = Color(0xFF3A2356),
    background = Color(0xFF2B2B2B),
    onBackground = Color(0xFFE6E6E6),
    surface = Color(0xFF2B2B2B),
    onSurface = Color(0xFFE6E6E6),
    surfaceVariant = Color(0xFF3F3F3F),
    onSurfaceVariant = Color(0xFFBDBDBD),
    surfaceContainerLowest = Color(0xFF232323),
    surfaceContainerLow = Color(0xFF2F2F2F),
    surfaceContainer = Color(0xFF353535),
    surfaceContainerHigh = Color(0xFF3B3B3B),
    surfaceContainerHighest = Color(0xFF444444),
    outline = Color(0xFF8F8F8F),
    outlineVariant = Color(0xFF555555),
    error = Color(0xFFFF8A80),
    onError = Color(0xFF5C0A05),
    errorContainer = Color(0xFF8C1D18),
    onErrorContainer = Color(0xFFFFDAD6),
)

/** Colors the Markdown preview needs beyond Material's roles. */
@Immutable
data class CodeColors(
    val background: Color,
    val keyword: Color,
    val string: Color,
    val comment: Color,
    val preprocessor: Color,
)

private val LightCode = CodeColors(
    background = Color(0xFFF2F2F2),
    keyword = Color(0xFF00008B),
    string = Color(0xFFA52A2A),
    comment = Color(0xFF006400),
    preprocessor = Color(0xFF8B008B),
)

private val DarkCode = CodeColors(
    background = Color(0xFF1B1B1B),
    keyword = Color(0xFF569CD6),
    string = Color(0xFFCE9178),
    comment = Color(0xFF6A9955),
    preprocessor = Color(0xFFC586C0),
)

val LocalCodeColors = staticCompositionLocalOf { LightCode }

@Composable
fun LexiconTheme(preference: ThemePreference, content: @Composable () -> Unit) {
    val dark = when (preference) {
        ThemePreference.System -> isSystemInDarkTheme()
        ThemePreference.Light -> false
        ThemePreference.Dark -> true
    }
    val colors: ColorScheme = if (dark) DarkColors else LightColors
    CompositionLocalProvider(LocalCodeColors provides if (dark) DarkCode else LightCode) {
        MaterialTheme(colorScheme = colors, content = content)
    }
}
