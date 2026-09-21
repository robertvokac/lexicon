package com.robertvokac.lexicon.ui.item

import android.net.Uri
import android.os.Bundle
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.compose.ui.text.TextRange
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconJson
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.ItemType
import com.robertvokac.lexicon.model.Link
import com.robertvokac.lexicon.model.Property
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.markdown.FormattingAction
import com.robertvokac.lexicon.ui.markdown.MarkdownFormatting
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.serialization.SerializationException
import java.util.concurrent.atomic.AtomicLong

enum class EditorTab(val label: String) {
    General("General"),
    Content("Content"),
    Values("Values"),
    Metadata("Metadata"),
    Links("Links"),
    Backlinks("Backlinks"),
}

sealed interface SaveStatus {
    data object Idle : SaveStatus
    data object Saving : SaveStatus
    data class Saved(val itemId: Int) : SaveStatus
    data class Failed(val message: String) : SaveStatus
}

/** A type switch waiting for the person to accept that field values go. */
data class TypeChangeRequest(
    val typeId: Int?,
    val groupId: Int?,
    val types: List<ItemType>,
    val affected: Int,
)

data class BlobStatus(val text: String, val busy: Boolean)

data class EditorState(
    val itemId: Int?,
    val loading: Boolean = true,
    val loadError: String? = null,
    val fields: EditorFields = EditorFields(),
    val groups: List<Group> = emptyList(),
    val types: List<ItemType> = emptyList(),
    /** The fields of the displayed type. */
    val typeFields: List<ItemField> = emptyList(),
    val fieldsLoadFailed: Boolean = false,
    val tab: EditorTab = EditorTab.General,
    val save: SaveStatus = SaveStatus.Idle,
    val typeChange: TypeChangeRequest? = null,
    /** Saved values a save would remove; the save waits for confirmation. */
    val saveNeedsTypeConfirmation: Int? = null,
    val blobs: Map<Int, BlobStatus> = emptyMap(),
    val tagSuggestions: List<String> = emptyList(),
    val flagSuggestions: List<String> = emptyList(),
    val aliasSuggestions: List<String> = emptyList(),
    val fromShare: Boolean = false,
    val message: String? = null,
) {
    val isNew: Boolean get() = itemId == null
    val saving: Boolean get() = save == SaveStatus.Saving

    /** A file is on its way to the server; saving now would leave it out. */
    val uploading: Boolean get() = blobs.values.any { it.busy }
}

/** Where a new item starts: from the list, Quick Add's "More", or a share. */
data class EditorStart(
    val itemId: Int?,
    val groupId: Int?,
    val typeId: Int?,
    val title: String,
    val content: String,
    val fromShare: Boolean,
)

/**
 * The full item editor: General, Content, Values, Metadata, Links and
 * Backlinks, saved as one atomic request. Nothing typed is lost on a failed
 * save; the state survives rotation and, while small enough, process death.
 */
class ItemEditorViewModel(
    private val container: AppContainer,
    private val start: EditorStart,
    private val savedState: SavedStateHandle,
) : ViewModel() {
    private val api = container.api
    private val transfer = BlobTransfer(container.api, container.contentResolver)
    private val _state = MutableStateFlow(EditorState(itemId = start.itemId, fromShare = start.fromShare))
    val state: StateFlow<EditorState> = _state.asStateFlow()

    /** The Markdown source. Kept outside [state] so typing never copies the whole note. */
    val content = TextFieldState()

    private var initialFields = EditorFields()
    private var initialContent = ""
    private var originalTypeId: Int? = null
    private var originalValues: Map<Int, String> = emptyMap()
    private var typeChangeConfirmed = false
    private var loadJob: Job? = null
    private val linkKeys = AtomicLong(1)

    init {
        savedState.setSavedStateProvider(SAVED_KEY) { saveInstanceState() }
        val restored = savedState.get<Bundle>(SAVED_KEY)?.getString(SNAPSHOT)?.let(::decodeSnapshot)
        load(restored)
        viewModelScope.launch {
            // An editor that could not load because the session expired
            // loads once the person has signed in again. A loaded one keeps
            // everything typed.
            container.sessions.state
                .map { it is SessionState.SignedIn }
                .distinctUntilChanged()
                .drop(1)
                .filter { it && _state.value.loadError != null }
                .collect { load(null) }
        }
    }

    // Loading ---------------------------------------------------------------

    fun retryLoad() = load(null)

    @OptIn(ExperimentalFoundationApi::class)
    private fun load(restored: EditorSnapshot?) {
        loadJob?.cancel()
        _state.update { it.copy(loading = true, loadError = null) }
        loadJob = viewModelScope.launch {
            try {
                val groups = api.groups()
                var fields: EditorFields
                var text: String
                if (start.itemId != null) {
                    val bundle = api.item(start.itemId, withLinks = true)
                    val item = bundle.item
                    originalTypeId = item.itemTypeId
                    originalValues = item.fieldValues.mapNotNull { (key, value) -> key.toIntOrNull()?.let { it to value } }.toMap()
                    fields = EditorFields(
                        groupId = item.groupId,
                        typeId = item.itemTypeId,
                        title = item.title,
                        disambiguation = item.disambiguation,
                        status = item.status,
                        understanding = item.understanding,
                        pinned = item.pinned,
                        tags = item.tags,
                        flags = item.flags,
                        aliases = item.aliases,
                        properties = item.properties,
                        values = originalValues,
                        links = EditorRules.sortedLinks(bundle.links.mapNotNull { it.toEntry(incoming = false) }),
                        backlinks = EditorRules.sortedLinks(bundle.backlinks.mapNotNull { it.toEntry(incoming = true) }),
                    )
                    text = item.content
                } else {
                    val groupId = start.groupId?.takeIf { id -> groups.any { it.id == id } }
                        ?: api.defaultGroupId()
                    fields = EditorFields(groupId = groupId, typeId = start.typeId, title = start.title)
                    text = start.content
                }
                val refreshedGroups = if (groups.any { it.id == fields.groupId }) groups else api.groups()
                initialFields = fields
                initialContent = text
                if (restored != null) {
                    // What the server holds decides the warnings; the snapshot
                    // decides what the editor shows.
                    fields = restored.fields
                    text = restored.content
                    initialFields = restored.initialFields
                    initialContent = restored.initialContent
                    typeChangeConfirmed = restored.typeChangeConfirmed
                }
                val types = api.types(fields.groupId)
                if (fields.typeId != null && types.none { it.id == fields.typeId }) {
                    fields = fields.copy(typeId = null, values = emptyMap())
                }
                val typeFields = fields.typeId?.let { api.fields(it) } ?: emptyList()
                content.setTextAndPlaceCursorAtEnd(text)
                content.undoState.clearHistory()
                _state.update {
                    it.copy(
                        loading = false,
                        fields = fields,
                        groups = refreshedGroups,
                        types = types,
                        typeFields = typeFields,
                        fieldsLoadFailed = false,
                    )
                }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, loadError = failure.userMessage() ?: "Sign in to continue.") }
            }
        }
    }

    private fun Link.toEntry(incoming: Boolean): LinkEntry? {
        val other = (if (incoming) fromItemId else toItemId) ?: return null
        return LinkEntry(
            key = linkKeys.getAndIncrement(),
            id = id,
            itemId = other,
            title = if (incoming) fromItemTitle else toItemTitle,
            linkType = linkType,
            customValue = customValue,
            position = position,
        )
    }

    // General ---------------------------------------------------------------

    private fun edit(change: (EditorFields) -> EditorFields) =
        _state.update { it.copy(fields = change(it.fields), save = if (it.save is SaveStatus.Failed) SaveStatus.Idle else it.save) }

    fun selectTab(tab: EditorTab) = _state.update { it.copy(tab = tab) }
    fun setTitle(value: String) = edit { it.copy(title = value) }
    fun setDisambiguation(value: String) = edit { it.copy(disambiguation = value) }
    fun setStatus(value: ItemStatus) = edit { it.copy(status = value) }
    fun setUnderstanding(value: UnderstandingLevel) = edit { it.copy(understanding = value) }
    fun setPinned(value: Boolean) = edit { it.copy(pinned = value) }

    /** Another group offers other types; a type it lacks is dropped, with the usual warning. */
    fun setGroup(groupId: Int?) {
        val current = _state.value
        if (groupId == current.fields.groupId) return
        viewModelScope.launch {
            val types = try {
                api.types(groupId)
            } catch (failure: ApiException) {
                _state.update { it.copy(message = failure.userMessage()) }
                return@launch
            }
            val typeId = _state.value.fields.typeId
            if (typeId == null || types.any { it.id == typeId }) {
                edit { it.copy(groupId = groupId) }
                _state.update { it.copy(types = types) }
            } else {
                requestTypeChange(null, groupId, types)
            }
        }
    }

    fun setType(typeId: Int?) = requestTypeChange(typeId, _state.value.fields.groupId, _state.value.types)

    private fun requestTypeChange(typeId: Int?, groupId: Int?, types: List<ItemType>) {
        val current = _state.value
        val displayed = current.fields.typeId
        if (typeId == displayed) {
            edit { it.copy(groupId = groupId) }
            _state.update { it.copy(types = types) }
            return
        }
        val affected = EditorRules.affectedByTypeChange(
            current.typeFields, current.fields.values, displayed, originalTypeId, originalValues, typeChangeConfirmed,
        )
        val request = TypeChangeRequest(typeId, groupId, types, affected)
        if (displayed != null && affected > 0) {
            _state.update { it.copy(typeChange = request) }
        } else {
            applyTypeChange(request)
        }
    }

    fun confirmTypeChange() {
        val request = _state.value.typeChange ?: return
        if (_state.value.fields.typeId == originalTypeId) typeChangeConfirmed = true
        _state.update { it.copy(typeChange = null) }
        applyTypeChange(request)
    }

    fun cancelTypeChange() = _state.update { it.copy(typeChange = null) }

    private fun applyTypeChange(request: TypeChangeRequest) {
        val oldFieldIds = _state.value.typeFields.mapNotNull { it.id }.toSet()
        edit { it.copy(groupId = request.groupId, typeId = request.typeId, values = it.values - oldFieldIds) }
        _state.update {
            it.copy(
                types = request.types,
                typeFields = emptyList(),
                tab = if (request.typeId == null && it.tab == EditorTab.Values) EditorTab.General else it.tab,
            )
        }
        val typeId = request.typeId ?: return
        viewModelScope.launch {
            try {
                val fields = api.fields(typeId)
                if (_state.value.fields.typeId == typeId) _state.update { it.copy(typeFields = fields, fieldsLoadFailed = false) }
            } catch (failure: ApiException) {
                _state.update { it.copy(fieldsLoadFailed = true, message = failure.userMessage()) }
            }
        }
    }

    // Values ----------------------------------------------------------------

    fun setValue(fieldId: Int, value: String) = edit { it.copy(values = it.values + (fieldId to value)) }

    /** Uploads the picked document at once, as the web client does, and stores its hash. */
    fun uploadBlob(fieldId: Int, uri: Uri) {
        if (_state.value.blobs[fieldId]?.busy == true) return
        setBlobStatus(fieldId, BlobStatus("Preparing upload…", busy = true))
        viewModelScope.launch {
            try {
                val document = transfer.describe(uri)
                val hash = transfer.upload(uri, document.size) { sent, total ->
                    val progress = if (total > 0) " ${sent * 100 / total}%" else " ${BlobTransfer.formatSize(sent)}"
                    setBlobStatus(fieldId, BlobStatus("Uploading ${document.name}$progress", busy = true))
                }
                setValue(fieldId, hash)
                setBlobStatus(fieldId, BlobStatus("${document.name} (${BlobTransfer.formatSize(document.size)})", busy = false))
            } catch (failure: ApiException) {
                setBlobStatus(fieldId, BlobStatus(failure.userMessage() ?: "Upload failed.", busy = false))
            } catch (failure: java.io.IOException) {
                setBlobStatus(fieldId, BlobStatus("The file could not be read: ${failure.message}", busy = false))
            } catch (failure: SecurityException) {
                setBlobStatus(fieldId, BlobStatus("The file could not be read: ${failure.message}", busy = false))
            }
        }
    }

    fun downloadBlob(fieldId: Int, hash: String, target: Uri) {
        if (_state.value.blobs[fieldId]?.busy == true) return
        setBlobStatus(fieldId, BlobStatus("Downloading…", busy = true))
        viewModelScope.launch {
            try {
                transfer.download(hash, target) { received, total ->
                    val progress = if (total > 0) "${received * 100 / total}%" else BlobTransfer.formatSize(received)
                    setBlobStatus(fieldId, BlobStatus("Downloading $progress", busy = true))
                }
                setBlobStatus(fieldId, BlobStatus("Saved.", busy = false))
            } catch (failure: ApiException) {
                setBlobStatus(fieldId, BlobStatus(failure.userMessage() ?: "Download failed.", busy = false))
            }
        }
    }

    fun clearBlob(fieldId: Int) {
        setValue(fieldId, "")
        _state.update { it.copy(blobs = it.blobs - fieldId) }
    }

    private fun setBlobStatus(fieldId: Int, status: BlobStatus) =
        _state.update { it.copy(blobs = it.blobs + (fieldId to status)) }

    // Content ---------------------------------------------------------------

    fun format(action: FormattingAction, codeLanguage: String = "") {
        val selection = content.selection
        val edit = MarkdownFormatting.edit(action, content.text, selection.start, selection.end, codeLanguage)
        content.edit {
            replace(edit.start, edit.end, edit.replacement)
            this.selection = TextRange(edit.selectionStart, edit.selectionEnd)
        }
        if (action == FormattingAction.CodeBlock) {
            viewModelScope.launch { container.settings.setCodeLanguage(codeLanguage.trim()) }
        }
    }

    suspend fun lastCodeLanguage(): String = container.settings.codeLanguage()

    // Metadata --------------------------------------------------------------

    fun loadSuggestions() {
        if (_state.value.tagSuggestions.isNotEmpty() || _state.value.flagSuggestions.isNotEmpty()) return
        viewModelScope.launch {
            try {
                val tags = api.tagUsage().map { it.value }
                val flags = api.flagUsage().map { it.value }
                val aliases = api.aliasUsage().map { it.value }
                _state.update { it.copy(tagSuggestions = tags, flagSuggestions = flags, aliasSuggestions = aliases) }
            } catch (_: ApiException) {
                // Suggestions are a convenience.
            }
        }
    }

    fun addTags(text: String) = edit { it.copy(tags = EditorRules.withValues(it.tags, text)) }
    fun editTag(index: Int, text: String) = edit { it.copy(tags = EditorRules.replaced(it.tags, index, text)) }
    fun removeTag(index: Int) = edit { it.copy(tags = it.tags.filterIndexed { i, _ -> i != index }) }
    fun addFlags(text: String) = edit { it.copy(flags = EditorRules.withValues(it.flags, text)) }
    fun editFlag(index: Int, text: String) = edit { it.copy(flags = EditorRules.replaced(it.flags, index, text)) }
    fun removeFlag(index: Int) = edit { it.copy(flags = it.flags.filterIndexed { i, _ -> i != index }) }
    fun addAlias(text: String) = edit { it.copy(aliases = EditorRules.withAlias(it.aliases, text)) }
    fun editAlias(index: Int, text: String) = edit { it.copy(aliases = EditorRules.replaced(it.aliases, index, text)) }
    fun removeAlias(index: Int) = edit { it.copy(aliases = it.aliases.filterIndexed { i, _ -> i != index }) }

    /** Null when stored, else why not. [index] is -1 for a new property. */
    fun putProperty(index: Int, key: String, value: String): String? {
        val properties = _state.value.fields.properties
        EditorRules.propertyProblem(properties, key, index)?.let { return it }
        val property = Property(key.trim(), value)
        edit {
            it.copy(
                properties = if (index < 0) it.properties + property
                else it.properties.toMutableList().also { list -> list[index] = property },
            )
        }
        return null
    }

    fun removeProperty(index: Int) = edit { it.copy(properties = it.properties.filterIndexed { i, _ -> i != index }) }

    // Links -----------------------------------------------------------------

    fun newLinkKey(): Long = linkKeys.getAndIncrement()

    fun putLink(incoming: Boolean, entry: LinkEntry) = edit { fields ->
        val list = if (incoming) fields.backlinks else fields.links
        val updated = EditorRules.sortedLinks(
            if (list.any { it.key == entry.key }) list.map { if (it.key == entry.key) entry else it } else list + entry,
        )
        if (incoming) fields.copy(backlinks = updated) else fields.copy(links = updated)
    }

    fun removeLink(incoming: Boolean, key: Long) = edit { fields ->
        if (incoming) fields.copy(backlinks = fields.backlinks.filter { it.key != key })
        else fields.copy(links = fields.links.filter { it.key != key })
    }

    // Saving ----------------------------------------------------------------

    /** Whether leaving now would lose anything. */
    fun hasUnsavedChanges(): Boolean {
        val current = _state.value
        if (current.loading || current.save is SaveStatus.Saved) return false
        return current.fields != initialFields || content.text.toString() != initialContent
    }

    fun save() {
        val current = _state.value
        if (current.saving || current.loading || current.loadError != null) return
        val problem = when {
            current.uploading -> "A file is still being transferred. Save when it has finished."
            current.fieldsLoadFailed -> "Cannot save while type fields failed to load."
            current.fields.groupId == null -> "Create at least one group first."
            current.fields.title.isBlank() -> "Title cannot be empty."
            else -> null
        }
        if (problem != null) {
            _state.update {
                it.copy(save = SaveStatus.Failed(problem), tab = if (it.fields.title.isBlank()) EditorTab.General else it.tab)
            }
            return
        }
        if (current.itemId != null && originalTypeId != current.fields.typeId && originalValues.isNotEmpty() && !typeChangeConfirmed) {
            _state.update { it.copy(saveNeedsTypeConfirmation = originalValues.size) }
            return
        }
        performSave()
    }

    fun confirmSaveTypeChange() {
        typeChangeConfirmed = true
        _state.update { it.copy(saveNeedsTypeConfirmation = null) }
        performSave()
    }

    fun cancelSaveTypeChange() = _state.update { it.copy(saveNeedsTypeConfirmation = null) }

    private fun performSave() {
        val current = _state.value
        if (current.saving) return
        val request = try {
            EditorRules.saveRequest(current.fields, content.text.toString(), current.typeFields)
        } catch (problem: IllegalArgumentException) {
            _state.update { it.copy(save = SaveStatus.Failed(problem.message ?: "The item cannot be saved.")) }
            return
        }
        _state.update { it.copy(save = SaveStatus.Saving) }
        viewModelScope.launch {
            try {
                // One request, one unit of work: the item and both link
                // directions are committed together or not at all.
                val saved = if (current.itemId != null) api.updateItem(current.itemId, request) else api.createItem(request)
                _state.update { it.copy(save = SaveStatus.Saved(saved.id)) }
                container.dataChanges.itemChanged(saved.id)
            } catch (failure: ApiException) {
                // Everything typed stays; the person can fix it or retry.
                _state.update {
                    it.copy(save = SaveStatus.Failed(failure.userMessage() ?: "Your session ended. Sign in again, then save."))
                }
            }
        }
    }

    fun messageShown() = _state.update { it.copy(message = null) }

    // Saved state -----------------------------------------------------------

    private fun saveInstanceState(): Bundle {
        val current = _state.value
        if (current.loading || current.loadError != null) return Bundle()
        val snapshot = EditorSnapshot(
            fields = current.fields,
            content = content.text.toString(),
            initialFields = initialFields,
            initialContent = initialContent,
            originalTypeId = originalTypeId,
            originalValues = originalValues,
            typeChangeConfirmed = typeChangeConfirmed,
        )
        val json = LexiconJson.encodeToString(EditorSnapshot.serializer(), snapshot)
        // A saved-state bundle is small by design. A huge note is not put in
        // it; the editor then reloads from the server after process death.
        return Bundle().apply { if (json.length <= MAX_SAVED_CHARS) putString(SNAPSHOT, json) }
    }

    private fun decodeSnapshot(json: String): EditorSnapshot? = try {
        LexiconJson.decodeFromString(EditorSnapshot.serializer(), json)
    } catch (_: SerializationException) {
        null
    }

    private companion object {
        const val SAVED_KEY = "item-editor"
        const val SNAPSHOT = "snapshot"
        const val MAX_SAVED_CHARS = 200_000
    }
}
