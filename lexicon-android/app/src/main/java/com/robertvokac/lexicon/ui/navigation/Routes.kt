package com.robertvokac.lexicon.ui.navigation

import kotlinx.serialization.Serializable

@Serializable
data object ItemsRoute

@Serializable
data class ItemRoute(val itemId: Int)

/** itemId -1 adds an item; groupId and typeId -1 mean "none chosen". */
@Serializable
data class EditItemRoute(
    val itemId: Int = -1,
    val groupId: Int = -1,
    val typeId: Int = -1,
    val title: String = "",
    val content: String = "",
    val fromShare: Boolean = false,
    val disambiguation: String = "",
)

@Serializable
data object GroupsRoute

@Serializable
data object TypesRoute

@Serializable
data class TypeRoute(val typeId: Int)

@Serializable
data class OverviewRoute(val kind: String)

@Serializable
data object SettingsRoute

@Serializable
data object ReviewRoute
