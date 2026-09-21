package com.robertvokac.lexicon.util

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/**
 * Tells open screens that something they show changed on the server, so a
 * list refreshes after an edit made on another screen. Nothing is cached:
 * listeners reload from the server.
 */
class DataChanges {
    enum class Kind { Items, Groups, Types }

    data class Change(val kind: Kind, val itemId: Int? = null)

    private val changes = MutableSharedFlow<Change>(extraBufferCapacity = 16)

    val events: SharedFlow<Change> = changes.asSharedFlow()

    fun itemChanged(itemId: Int?) {
        changes.tryEmit(Change(Kind.Items, itemId))
    }

    fun groupsChanged() {
        changes.tryEmit(Change(Kind.Groups))
    }

    fun typesChanged() {
        changes.tryEmit(Change(Kind.Types))
    }
}
