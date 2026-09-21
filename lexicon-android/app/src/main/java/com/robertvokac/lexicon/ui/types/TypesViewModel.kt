package com.robertvokac.lexicon.ui.types

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.FieldWrite
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemType
import com.robertvokac.lexicon.model.TypeWrite
import com.robertvokac.lexicon.ui.common.userMessage
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** A destructive change waiting for confirmation, with the count the server reported. */
sealed interface TypesConfirmation {
    val message: String

    data class DeleteType(val type: ItemType, val count: Int) : TypesConfirmation {
        override val message: String
            get() = "Delete type '${type.name}'? The Type field will be set to None for all $count item(s) using it, " +
                "and their custom field values will be deleted. This data cannot be restored automatically."
    }

    data class DeleteField(val field: ItemField, val count: Int) : TypesConfirmation {
        override val message: String
            // "this." because a bare "field" in a getter is the backing field.
            get() = "Delete field '${this.field.name}'? This will remove its value from $count item(s). Continue?"
    }

    data class ChangeField(val field: ItemField, val write: FieldWrite, val count: Int) : TypesConfirmation {
        override val message: String
            get() = "Changing the data type or enum options will clear $count stored value(s). Continue?"
    }
}

data class TypesState(
    val loading: Boolean = true,
    val error: String? = null,
    val types: List<ItemType> = emptyList(),
    val groups: List<Group> = emptyList(),
    val selectedTypeId: Int? = null,
    val fields: List<ItemField> = emptyList(),
    val fieldsLoading: Boolean = false,
    val fieldsError: String? = null,
    val busy: Boolean = false,
    val confirmation: TypesConfirmation? = null,
    val message: String? = null,
) {
    val selectedType: ItemType? get() = types.firstOrNull { it.id == selectedTypeId }
}

/** The counterpart of ItemTypeManagerDialog, including its destructive-change warnings. */
class TypesViewModel(private val container: AppContainer) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(TypesState())
    val state: StateFlow<TypesState> = _state.asStateFlow()
    private var fieldsJob: Job? = null

    init {
        load()
    }

    fun load() {
        viewModelScope.launch {
            try {
                val groups = api.groups()
                val types = api.types()
                _state.update { it.copy(loading = false, error = null, groups = groups, types = types) }
                _state.value.selectedTypeId?.let(::loadFields)
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage()) }
            }
        }
    }

    fun select(typeId: Int?) {
        _state.update { it.copy(selectedTypeId = typeId, fields = emptyList()) }
        if (typeId != null) loadFields(typeId)
    }

    private fun loadFields(typeId: Int) {
        fieldsJob?.cancel()
        _state.update { it.copy(fieldsLoading = true, fieldsError = null) }
        fieldsJob = viewModelScope.launch {
            try {
                val fields = api.fields(typeId)
                if (_state.value.selectedTypeId == typeId) _state.update { it.copy(fields = fields, fieldsLoading = false) }
            } catch (failure: ApiException) {
                _state.update { it.copy(fieldsLoading = false, fieldsError = failure.userMessage()) }
            }
        }
    }

    fun nextFieldPosition(): Int = (_state.value.fields.maxOfOrNull { it.position } ?: -1) + 1

    private fun mutate(onDone: () -> Unit = {}, block: suspend () -> String?) {
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                val message = block()
                _state.update { it.copy(busy = false, message = message) }
                container.dataChanges.typesChanged()
                onDone()
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    fun saveType(id: Int?, type: TypeWrite, onDone: () -> Unit) = mutate(onDone) {
        val saved = if (id == null) api.createType(type) else api.updateType(id, type)
        _state.update { it.copy(types = api.types(), selectedTypeId = if (id == null) saved.id else it.selectedTypeId) }
        if (id == null) saved.id?.let(::loadFields)
        null
    }

    /** Asks the server how many items use the type before asking the person. */
    fun requestDeleteType(type: ItemType) = count { TypesConfirmation.DeleteType(type, api.typeItemCount(requireNotNull(type.id))) }

    fun requestDeleteField(field: ItemField) = count { TypesConfirmation.DeleteField(field, api.fieldValueCount(requireNotNull(field.id))) }

    /**
     * Changing a field's data type or enum options clears its stored values,
     * so the server's count decides whether to ask first.
     */
    fun saveField(existing: ItemField?, write: FieldWrite, onDone: () -> Unit) {
        val typeId = _state.value.selectedTypeId ?: return
        if (existing == null) {
            mutate(onDone) {
                api.createField(typeId, write)
                reloadFields(typeId)
                null
            }
            return
        }
        val destructive = write.dataType != existing.dataType || write.enumOptions != existing.enumOptions
        if (!destructive) {
            mutate(onDone) {
                api.updateField(requireNotNull(existing.id), write.copy(itemTypeId = existing.itemTypeId))
                reloadFields(typeId)
                null
            }
            return
        }
        viewModelScope.launch {
            try {
                val count = api.fieldValueCount(requireNotNull(existing.id))
                if (count > 0) {
                    _state.update { it.copy(confirmation = TypesConfirmation.ChangeField(existing, write, count)) }
                    onDone()
                } else {
                    mutate(onDone) {
                        api.updateField(existing.id, write.copy(itemTypeId = existing.itemTypeId))
                        reloadFields(typeId)
                        null
                    }
                }
            } catch (failure: ApiException) {
                _state.update { it.copy(message = failure.userMessage()) }
            }
        }
    }

    private fun count(build: suspend () -> TypesConfirmation) {
        viewModelScope.launch {
            try {
                val confirmation = build()
                _state.update { it.copy(confirmation = confirmation) }
            } catch (failure: ApiException) {
                _state.update { it.copy(message = failure.userMessage()) }
            }
        }
    }

    fun cancelConfirmation() = _state.update { it.copy(confirmation = null) }

    fun confirm() {
        val confirmation = _state.value.confirmation ?: return
        _state.update { it.copy(confirmation = null) }
        when (confirmation) {
            is TypesConfirmation.DeleteType -> mutate {
                api.deleteType(requireNotNull(confirmation.type.id))
                _state.update {
                    it.copy(
                        types = api.types(),
                        selectedTypeId = it.selectedTypeId.takeIf { id -> id != confirmation.type.id },
                        fields = if (it.selectedTypeId == confirmation.type.id) emptyList() else it.fields,
                    )
                }
                "Deleted type '${confirmation.type.name}'."
            }
            is TypesConfirmation.DeleteField -> mutate {
                api.deleteField(requireNotNull(confirmation.field.id))
                confirmation.field.itemTypeId?.let { reloadFields(it) }
                "Deleted field '${confirmation.field.name}'."
            }
            is TypesConfirmation.ChangeField -> mutate {
                val field = confirmation.field
                api.updateField(requireNotNull(field.id), confirmation.write.copy(itemTypeId = field.itemTypeId))
                field.itemTypeId?.let { reloadFields(it) }
                null
            }
        }
    }

    private suspend fun reloadFields(typeId: Int) {
        val fields = api.fields(typeId)
        if (_state.value.selectedTypeId == typeId) _state.update { it.copy(fields = fields) }
    }

    fun messageShown() = _state.update { it.copy(message = null) }
}
