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
    val content: String = "",
    val revision: Int = 0,
    val editing: Boolean = false,
    val saving: Boolean = false,
    val saveError: String? = null,
    val conflict: Boolean = false,
)

class BoardViewModel(private val container: AppContainer) : ViewModel() {
    private val _state = MutableStateFlow(BoardUiState())
    val state: StateFlow<BoardUiState> = _state.asStateFlow()
    val editor = TextFieldState()

    init {
        load()
    }

    fun retry() = load()

    @OptIn(ExperimentalFoundationApi::class)
    private fun load() {
        _state.update { it.copy(loading = true, loadError = null, conflict = false) }
        viewModelScope.launch {
            try {
                val board = container.api.board()
                editor.setTextAndPlaceCursorAtEnd(board.content)
                editor.undoState.clearHistory()
                _state.value = BoardUiState(content = board.content, revision = board.revision, loading = false)
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, loadError = failure.userMessage() ?: "Sign in to continue.") }
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

    fun reloadAfterConflict() = load()

    fun keepEditingAfterConflict() = _state.update { it.copy(conflict = false) }

    private fun saveWithRevision(revision: Int) {
        if (_state.value.saving) return
        _state.update { it.copy(saving = true, saveError = null) }
        viewModelScope.launch {
            try {
                val saved = container.api.saveBoard(Board(editor.text.toString(), revision))
                _state.update {
                    it.copy(
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
