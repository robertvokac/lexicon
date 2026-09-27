package com.robertvokac.lexicon.ui.board

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.compose.ui.text.TextRange
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.Board
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.markdown.FormattingAction
import com.robertvokac.lexicon.ui.markdown.MarkdownFormatting
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class BoardUiState(
    val loading: Boolean = true,
    val loadError: String? = null,
    val boards: List<Board> = emptyList(),
    val selectedId: Int = -1,
    val content: String = "",
    val revision: Int = 0,
    val editing: Boolean = false,
    val saving: Boolean = false,
    val saveError: String? = null,
    val conflict: Boolean = false,
) {
    val selected: Board? get() = boards.firstOrNull { it.id == selectedId }
}

class BoardViewModel(private val container: AppContainer) : ViewModel() {
    private val _state = MutableStateFlow(BoardUiState())
    val state: StateFlow<BoardUiState> = _state.asStateFlow()
    val editor = TextFieldState()

    init {
        load()
    }

    fun retry() = load(_state.value.selectedId)

    @OptIn(ExperimentalFoundationApi::class)
    private fun load(preferredId: Int = _state.value.selectedId) {
        _state.update { it.copy(loading = true, loadError = null, conflict = false) }
        viewModelScope.launch {
            try {
                refresh(preferredId)
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, loadError = failure.userMessage() ?: "Sign in to continue.") }
            }
        }
    }

    @OptIn(ExperimentalFoundationApi::class)
    private suspend fun refresh(preferredId: Int = -1) {
        var boards = container.api.boards()
        if (boards.isEmpty()) boards = listOf(container.api.createBoard("Main"))
        val board = boards.firstOrNull { it.id == preferredId } ?: boards.first()
        editor.setTextAndPlaceCursorAtEnd(board.content)
        editor.undoState.clearHistory()
        _state.value = BoardUiState(
            boards = boards,
            selectedId = board.id,
            content = board.content,
            revision = board.revision,
            loading = false,
        )
    }

    @OptIn(ExperimentalFoundationApi::class)
    fun select(id: Int) {
        if (_state.value.editing || _state.value.saving) return
        val board = _state.value.boards.firstOrNull { it.id == id } ?: return
        editor.setTextAndPlaceCursorAtEnd(board.content)
        editor.undoState.clearHistory()
        _state.update {
            it.copy(selectedId = board.id, content = board.content, revision = board.revision, saveError = null)
        }
    }

    fun create(name: String) = manage {
        val created = container.api.createBoard(name.trim())
        refresh(created.id)
    }

    fun rename(name: String) = manage {
        val board = _state.value.selected ?: return@manage
        val saved = container.api.saveBoard(board.copy(name = name.trim()))
        refresh(saved.id)
    }

    fun deleteSelected() = manage {
        val board = _state.value.selected ?: return@manage
        container.api.deleteBoard(board.id)
        refresh()
    }

    private fun manage(operation: suspend () -> Unit) {
        if (_state.value.saving) return
        _state.update { it.copy(saving = true, saveError = null) }
        viewModelScope.launch {
            try {
                operation()
                _state.update { it.copy(saving = false) }
            } catch (failure: ApiException) {
                _state.update {
                    it.copy(saving = false, saveError = failure.userMessage() ?: "Sign in to continue.")
                }
            }
        }
    }

    @OptIn(ExperimentalFoundationApi::class)
    fun edit() {
        editor.setTextAndPlaceCursorAtEnd(_state.value.content)
        editor.undoState.clearHistory()
        _state.update { it.copy(editing = true, saveError = null) }
    }

    fun cancelEdit() = _state.update { it.copy(editing = false, saveError = null, conflict = false) }

    fun hasUnsavedChanges(): Boolean = editor.text.toString() != _state.value.content

    fun save() = saveWithRevision(_state.value.revision)

    fun overwriteConflict() {
        _state.update { it.copy(conflict = false) }
        saveWithRevision(0)
    }

    fun reloadAfterConflict() = load(_state.value.selectedId)

    fun keepEditingAfterConflict() = _state.update { it.copy(conflict = false) }

    private fun saveWithRevision(revision: Int) {
        if (_state.value.saving) return
        _state.update { it.copy(saving = true, saveError = null) }
        viewModelScope.launch {
            try {
                val current = _state.value.selected ?: run {
                    _state.update { it.copy(saving = false, saveError = "Choose a Board first.") }
                    return@launch
                }
                val saved = container.api.saveBoard(
                    current.copy(content = editor.text.toString(), revision = revision),
                )
                _state.update {
                    it.copy(
                        boards = it.boards.map { board -> if (board.id == saved.id) saved else board },
                        content = saved.content,
                        revision = saved.revision,
                        editing = false,
                        saving = false,
                        conflict = false,
                    )
                }
            } catch (_: ApiException.Conflict) {
                _state.update { it.copy(saving = false, conflict = true) }
            } catch (failure: ApiException) {
                _state.update {
                    it.copy(saving = false, saveError = failure.userMessage() ?: "Sign in to continue.")
                }
            }
        }
    }

    fun format(action: FormattingAction, codeLanguage: String = "") {
        val selection = editor.selection
        val edit = MarkdownFormatting.edit(action, editor.text, selection.start, selection.end, codeLanguage)
        editor.edit {
            replace(edit.start, edit.end, edit.replacement)
            this.selection = TextRange(edit.selectionStart, edit.selectionEnd)
        }
        if (action == FormattingAction.CodeBlock) {
            viewModelScope.launch { container.settings.setCodeLanguage(codeLanguage.trim()) }
        }
    }

    suspend fun lastCodeLanguage(): String = container.settings.codeLanguage()

    fun insertItemLink(target: String) {
        val selection = editor.selection
        val selected = editor.text.substring(selection.min, selection.max).trim()
        val text = "[[" + target + (if (selected.isNotEmpty() && selected != target) "|$selected" else "") + "]]"
        editor.edit {
            replace(selection.min, selection.max, text)
            this.selection = TextRange(selection.min + text.length)
        }
    }
}
