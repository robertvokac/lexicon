// The Android application lives in :app. Built-in Kotlin in AGP 9 uses the
// Kotlin Gradle plugin on this classpath; the Compose compiler plugin brings
// the version pinned in gradle/libs.versions.toml.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.kotlin.serialization) apply false
}
