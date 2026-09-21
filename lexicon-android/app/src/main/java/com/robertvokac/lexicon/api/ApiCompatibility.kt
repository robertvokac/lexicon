package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.model.Health

/** Decides whether a server's /health answer is one this client can talk to. */
object ApiCompatibility {
    sealed interface Result {
        data object Compatible : Result
        data class Incompatible(val message: String) : Result
    }

    fun check(health: Health, server: ServerUrl): Result = when {
        health.application.isNotEmpty() && health.application != "Lexicon" -> Result.Incompatible(
            "${server.value} is not a LexiconServer (it reports \"${health.application}\").",
        )
        health.apiVersion != LexiconApi.API_VERSION -> Result.Incompatible(
            "This Lexicon app speaks API version ${LexiconApi.API_VERSION}, but the server at " +
                "${server.value} reports version ${health.apiVersion}. Update the app or the server " +
                "so the versions match.",
        )
        else -> Result.Compatible
    }
}
