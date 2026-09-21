package com.robertvokac.lexicon.api

/** The signed-in session the API client authenticates with. */
class Session(val server: ServerUrl, val username: String, val token: String) {
    // The token never appears in logs, crash reports or debugger summaries.
    override fun toString(): String = "Session(server=$server, username=$username, token=<redacted>)"
}

/** How ApiClient reaches the current session without owning it. */
interface SessionAccess {
    /** The session to authenticate with, or null when signed out. */
    val current: Session?

    /**
     * Called once when the server answers 401 to a request made with
     * [session]. Implementations drop that session if it is still current;
     * a newer session is left alone.
     */
    fun onUnauthorized(session: Session)
}
