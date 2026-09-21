package com.robertvokac.lexicon.api

import kotlinx.serialization.json.Json

/** JSON settings shared by every request and response. */
val LexiconJson: Json = Json {
    // A newer server may add fields; that is not a reason to fail.
    ignoreUnknownKeys = true
    // Absent IDs travel as null and every query field is sent explicitly.
    encodeDefaults = true
    explicitNulls = true
    // An unknown enum name is an incompatibility, never silently defaulted.
    coerceInputValues = false
}
