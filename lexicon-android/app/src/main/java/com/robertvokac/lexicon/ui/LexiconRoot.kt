package com.robertvokac.lexicon.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Label
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.automirrored.filled.Logout
import androidx.compose.material.icons.filled.Category
import androidx.compose.material.icons.filled.Flag
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Translate
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.NavigationDrawerItem
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.VerticalDivider
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfoV2
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.toRoute
import androidx.window.core.layout.WindowSizeClass
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.auth.Identity
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.share.LaunchRequest
import com.robertvokac.lexicon.storage.ThemePreference
import com.robertvokac.lexicon.ui.common.LocalAppContainer
import com.robertvokac.lexicon.ui.common.lexiconViewModel
import com.robertvokac.lexicon.ui.groups.GroupsScreen
import com.robertvokac.lexicon.ui.groups.GroupsViewModel
import com.robertvokac.lexicon.ui.item.EditorStart
import com.robertvokac.lexicon.ui.item.ItemDetailScreen
import com.robertvokac.lexicon.ui.item.ItemDetailViewModel
import com.robertvokac.lexicon.ui.item.ItemEditorScreen
import com.robertvokac.lexicon.ui.item.ItemEditorViewModel
import com.robertvokac.lexicon.ui.items.ItemsScreen
import com.robertvokac.lexicon.ui.items.ItemsViewModel
import com.robertvokac.lexicon.ui.login.CompatibilityScreen
import com.robertvokac.lexicon.ui.login.LoginScreen
import com.robertvokac.lexicon.ui.login.LoginViewModel
import com.robertvokac.lexicon.ui.login.StartingScreen
import com.robertvokac.lexicon.ui.login.UnreachableScreen
import com.robertvokac.lexicon.ui.navigation.EditItemRoute
import com.robertvokac.lexicon.ui.navigation.GroupsRoute
import com.robertvokac.lexicon.ui.navigation.ItemRoute
import com.robertvokac.lexicon.ui.navigation.ItemsRoute
import com.robertvokac.lexicon.ui.navigation.OverviewRoute
import com.robertvokac.lexicon.ui.navigation.SettingsRoute
import com.robertvokac.lexicon.ui.navigation.TypeRoute
import com.robertvokac.lexicon.ui.navigation.TypesRoute
import com.robertvokac.lexicon.ui.overview.OverviewKind
import com.robertvokac.lexicon.ui.overview.OverviewScreen
import com.robertvokac.lexicon.ui.overview.OverviewViewModel
import com.robertvokac.lexicon.ui.settings.SettingsScreen
import com.robertvokac.lexicon.ui.settings.SettingsViewModel
import com.robertvokac.lexicon.ui.theme.LexiconTheme
import com.robertvokac.lexicon.ui.types.TypeDetailScreen
import com.robertvokac.lexicon.ui.types.TypesScreen
import com.robertvokac.lexicon.ui.types.TypesViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch

/**
 * Keeps the ViewModels of one signed-in identity. They survive rotation and a
 * session that expires underneath them, and are cleared when someone else
 * signs in or the person logs out.
 */
class IdentityStores : ViewModel() {
    private var key: String? = null
    private var store: ViewModelStore? = null

    fun storeFor(identityKey: String): ViewModelStore {
        if (identityKey != key) {
            store?.clear()
            store = ViewModelStore()
            key = identityKey
        }
        return requireNotNull(store)
    }

    fun drop() {
        store?.clear()
        store = null
        key = null
    }

    override fun onCleared() = drop()
}

/**
 * Hands over requests from outside (a share, the Quick Add shortcut) once
 * someone is signed in. Lives as long as the Activity.
 */
class LaunchRequests : ViewModel() {
    val pending = MutableStateFlow<LaunchRequest?>(null)
    val quickAdd = MutableStateFlow(false)
}

@Composable
fun LexiconRoot(
    container: AppContainer,
    launchRequests: LaunchRequests,
    onShareFinished: (saved: Boolean) -> Unit,
    onExit: () -> Unit,
    onDarkThemeChanged: (Boolean) -> Unit = {},
) {
    val theme by container.settings.theme.collectAsStateWithLifecycle(ThemePreference.System)
    LexiconTheme(theme) {
        val dark = MaterialTheme.colorScheme.surface.luminanceIsDark()
        LaunchedEffect(dark) { onDarkThemeChanged(dark) }
        CompositionLocalProvider(LocalAppContainer provides container) {
            LaunchedEffect(Unit) { container.sessions.start() }
            val session by container.sessions.state.collectAsStateWithLifecycle()
            val stores: IdentityStores = viewModel()

            // The identity whose screens stay composed: the signed-in one, or
            // one whose session expired underneath the person.
            val identity: Identity? = when (val current = session) {
                is SessionState.SignedIn -> current.identity
                is SessionState.SignedOut -> current.retained
                else -> null
            }
            LaunchedEffect(identity) { if (identity == null) stores.drop() }

            Box(Modifier.fillMaxSize()) {
                if (identity != null) {
                    val signedIn = session is SessionState.SignedIn
                    key(identity.key) {
                        val store = stores.storeFor(identity.key)
                        val owner = remember(store) { object : ViewModelStoreOwner { override val viewModelStore = store } }
                        CompositionLocalProvider(LocalViewModelStoreOwner provides owner) {
                            Box(if (signedIn) Modifier.fillMaxSize() else Modifier.fillMaxSize().clearAndSetSemantics { }) {
                                MainScaffold(identity, launchRequests, signedIn, onShareFinished)
                            }
                        }
                    }
                }
                when (val current = session) {
                    SessionState.Restoring -> StartingScreen()
                    is SessionState.SignedOut -> {
                        val login: LoginViewModel = lexiconViewModel(key = "login") { app, _ -> LoginViewModel(app) }
                        BackHandler(onBack = onExit)
                        LoginScreen(login, notice = current.message)
                    }
                    is SessionState.Incompatible -> CompatibilityScreen(
                        server = current.server.value,
                        message = current.message,
                        onRetry = { container.sessions.retry() },
                        onChangeServer = { container.sessions.abandonStoredSession() },
                    )
                    is SessionState.Unreachable -> UnreachableScreen(
                        server = current.server.value,
                        message = current.message,
                        onRetry = { container.sessions.retry() },
                        onSignIn = { container.sessions.abandonStoredSession() },
                    )
                    is SessionState.SignedIn -> Unit
                }
            }
        }
    }
}

private fun androidx.compose.ui.graphics.Color.luminanceIsDark(): Boolean =
    (0.2126f * red + 0.7152f * green + 0.0722f * blue) < 0.5f

private enum class Destination(val label: String) {
    Items("Items"),
    Groups("Groups"),
    Types("Types"),
    Tags("All tags"),
    Flags("All flags"),
    Aliases("All aliases"),
    Settings("Settings"),
}

@Composable
private fun MainScaffold(
    identity: Identity,
    launchRequests: LaunchRequests,
    signedIn: Boolean,
    onShareFinished: (Boolean) -> Unit,
) {
    val container = LocalAppContainer.current
    val navController = rememberNavController()
    val drawerState = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    val widthClass = currentWindowAdaptiveInfoV2().windowSizeClass
    val wide = widthClass.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_EXPANDED_LOWER_BOUND)

    // Requests from outside wait until someone is signed in.
    val pending by launchRequests.pending.collectAsStateWithLifecycle()
    LaunchedEffect(pending, signedIn) {
        val request = pending ?: return@LaunchedEffect
        if (!signedIn) return@LaunchedEffect
        launchRequests.pending.value = null
        when (request) {
            LaunchRequest.QuickAdd -> {
                navController.navigate(ItemsRoute) { popUpTo<ItemsRoute> { inclusive = false } }
                launchRequests.quickAdd.value = true
            }
            is LaunchRequest.Share -> navController.navigate(
                EditItemRoute(title = request.prefill.title, content = request.prefill.content, fromShare = true),
            )
        }
    }

    fun go(destination: Destination) {
        scope.launch { drawerState.close() }
        val route: Any = when (destination) {
            Destination.Items -> ItemsRoute
            Destination.Groups -> GroupsRoute
            Destination.Types -> TypesRoute
            Destination.Tags -> OverviewRoute(OverviewKind.Tags.name)
            Destination.Flags -> OverviewRoute(OverviewKind.Flags.name)
            Destination.Aliases -> OverviewRoute(OverviewKind.Aliases.name)
            Destination.Settings -> SettingsRoute
        }
        navController.navigate(route) {
            popUpTo<ItemsRoute> { inclusive = false }
            launchSingleTop = true
        }
    }

    ModalNavigationDrawer(
        drawerState = drawerState,
        gesturesEnabled = drawerState.isOpen,
        drawerContent = {
            ModalDrawerSheet {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    Text(
                        "Lexicon",
                        style = MaterialTheme.typography.titleLarge,
                        modifier = Modifier.padding(start = 28.dp, top = 24.dp).semantics { heading() },
                    )
                    Text(
                        "${identity.username} @ ${identity.server.value}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(start = 28.dp, end = 16.dp, bottom = 12.dp),
                    )
                    DrawerEntry(Destination.Items, Icons.AutoMirrored.Filled.List, ::go)
                    DrawerHeading("Manage")
                    DrawerEntry(Destination.Groups, Icons.Filled.Folder, ::go)
                    DrawerEntry(Destination.Types, Icons.Filled.Category, ::go)
                    DrawerHeading("Overview")
                    DrawerEntry(Destination.Tags, Icons.AutoMirrored.Filled.Label, ::go)
                    DrawerEntry(Destination.Flags, Icons.Filled.Flag, ::go)
                    DrawerEntry(Destination.Aliases, Icons.Filled.Translate, ::go)
                    HorizontalDivider(Modifier.padding(vertical = 8.dp))
                    DrawerEntry(Destination.Settings, Icons.Filled.Settings, ::go)
                    NavigationDrawerItem(
                        label = { Text("Log out") },
                        icon = { Icon(Icons.AutoMirrored.Filled.Logout, contentDescription = null) },
                        selected = false,
                        onClick = {
                            scope.launch { drawerState.close() }
                            container.sessions.logout()
                        },
                        modifier = Modifier.padding(horizontal = 12.dp),
                    )
                }
            }
        },
    ) {
        Surface(Modifier.fillMaxSize()) {
            LexiconNavHost(navController, wide, launchRequests, onOpenDrawer = { scope.launch { drawerState.open() } }, onShareFinished)
        }
    }
}

@Composable
private fun DrawerHeading(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(start = 28.dp, top = 16.dp, bottom = 8.dp).semantics { heading() },
    )
}

@Composable
private fun DrawerEntry(destination: Destination, icon: androidx.compose.ui.graphics.vector.ImageVector, onClick: (Destination) -> Unit) {
    NavigationDrawerItem(
        label = { Text(destination.label) },
        icon = { Icon(icon, contentDescription = null) },
        selected = false,
        onClick = { onClick(destination) },
        modifier = Modifier.padding(horizontal = 12.dp),
    )
}

@Composable
private fun LexiconNavHost(
    navController: NavHostController,
    wide: Boolean,
    launchRequests: LaunchRequests,
    onOpenDrawer: () -> Unit,
    onShareFinished: (Boolean) -> Unit,
) {
    val back: () -> Unit = { navController.popBackStack() }
    NavHost(navController, startDestination = ItemsRoute) {
        composable<ItemsRoute> {
            val items = lexiconViewModel { app, _ -> ItemsViewModel(app) }
            val state by items.state.collectAsStateWithLifecycle()
            val quickAdd by launchRequests.quickAdd.collectAsStateWithLifecycle()
            LaunchedEffect(quickAdd) {
                if (quickAdd) {
                    launchRequests.quickAdd.value = false
                    items.openQuickAdd("")
                }
            }
            val edit: (Int) -> Unit = { navController.navigate(EditItemRoute(itemId = it)) }
            val add: (com.robertvokac.lexicon.ui.items.NewItemRequest) -> Unit = { request ->
                navController.navigate(EditItemRoute(groupId = request.groupId ?: -1, typeId = request.typeId ?: -1, title = request.title))
            }
            if (wide) {
                Row(Modifier.fillMaxSize()) {
                    ItemsScreen(
                        viewModel = items,
                        onOpenDrawer = onOpenDrawer,
                        onOpenItem = items::select,
                        onEditItem = edit,
                        onAddItem = add,
                        selectedItemId = state.selectedItemId,
                        modifier = Modifier.weight(0.42f).fillMaxHeight(),
                    )
                    VerticalDivider()
                    Box(Modifier.weight(0.58f).fillMaxHeight(), contentAlignment = Alignment.Center) {
                        val selected = state.selectedItemId
                        if (selected == null) {
                            Text("Select an item to read it here.", color = MaterialTheme.colorScheme.onSurfaceVariant)
                        } else {
                            val detail = lexiconViewModel(key = "detail-pane") { app, _ -> ItemDetailViewModel(app, selected) }
                            LaunchedEffect(selected) { detail.open(selected) }
                            ItemDetailScreen(
                                viewModel = detail,
                                onBack = null,
                                onEdit = { edit(selected) },
                                onOpenItem = items::select,
                                onDeleted = { items.select(null) },
                            )
                        }
                    }
                }
            } else {
                ItemsScreen(
                    viewModel = items,
                    onOpenDrawer = onOpenDrawer,
                    onOpenItem = { navController.navigate(ItemRoute(it)) },
                    onEditItem = edit,
                    onAddItem = add,
                )
            }
        }
        composable<ItemRoute> { entry ->
            val route = entry.toRoute<ItemRoute>()
            val detail = lexiconViewModel { app, _ -> ItemDetailViewModel(app, route.itemId) }
            ItemDetailScreen(
                viewModel = detail,
                onBack = back,
                onEdit = { navController.navigate(EditItemRoute(itemId = route.itemId)) },
                onOpenItem = { navController.navigate(ItemRoute(it)) },
                onDeleted = back,
            )
        }
        composable<EditItemRoute> { entry ->
            val route = entry.toRoute<EditItemRoute>()
            val editor = lexiconViewModel { app, saved ->
                ItemEditorViewModel(
                    app,
                    EditorStart(
                        itemId = route.itemId.takeIf { it > 0 },
                        groupId = route.groupId.takeIf { it > 0 },
                        typeId = route.typeId.takeIf { it > 0 },
                        title = route.title,
                        content = route.content,
                        fromShare = route.fromShare,
                    ),
                    saved,
                )
            }
            ItemEditorScreen(editor) { savedId ->
                when {
                    route.fromShare -> onShareFinished(savedId != null)
                    savedId != null && route.itemId <= 0 -> navController.navigate(ItemRoute(savedId)) {
                        popUpTo<EditItemRoute> { inclusive = true }
                    }
                    else -> navController.popBackStack()
                }
            }
        }
        composable<GroupsRoute> {
            GroupsScreen(lexiconViewModel { app, _ -> GroupsViewModel(app) }, onBack = back)
        }
        composable<TypesRoute> {
            val types = lexiconViewModel { app, _ -> TypesViewModel(app) }
            TypesScreen(types, wide = wide, onBack = back, onOpenType = { navController.navigate(TypeRoute(it)) })
        }
        composable<TypeRoute> { entry ->
            val route = entry.toRoute<TypeRoute>()
            // Shares the type list's ViewModel, so both screens show one state.
            val parent = remember(entry) { navController.getBackStackEntry<TypesRoute>() }
            val types: TypesViewModel = viewModel(parent, factory = typesFactory(LocalAppContainer.current))
            TypeDetailScreen(types, route.typeId, onBack = back)
        }
        composable<OverviewRoute> { entry ->
            val kind = runCatching { OverviewKind.valueOf(entry.toRoute<OverviewRoute>().kind) }.getOrDefault(OverviewKind.Tags)
            OverviewScreen(lexiconViewModel(key = kind.name) { app, _ -> OverviewViewModel(app.api, kind) }, kind, onBack = back)
        }
        composable<SettingsRoute> {
            SettingsScreen(lexiconViewModel { app, _ -> SettingsViewModel(app) }, onBack = back)
        }
    }
}

private fun typesFactory(container: AppContainer) = viewModelFactory { initializer { TypesViewModel(container) } }
