package com.robertvokac.lexicon.ui.common

import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.InputTransformation
import androidx.compose.foundation.text.input.KeyboardActionHandler
import androidx.compose.foundation.text.input.TextFieldLineLimits
import androidx.compose.foundation.text.input.rememberTextFieldState
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier

/**
 * An outlined text field on the TextFieldState API with the familiar
 * value/onValueChange contract. Dialogs and sheets use it: the state-based
 * field is the current text input stack, and its focus handling in a separate
 * dialog window is the one Compose's own tests exercise.
 *
 * [accept] restricts what can be typed (digits only, say); rejected edits are
 * undone as they happen, so [onValueChange] only ever sees acceptable text.
 */
@Composable
fun SyncedTextField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    label: String? = null,
    placeholder: String? = null,
    supportingText: String? = null,
    isError: Boolean = false,
    enabled: Boolean = true,
    readOnly: Boolean = false,
    singleLine: Boolean = true,
    minLines: Int = 1,
    keyboardOptions: KeyboardOptions = LiteralTextKeyboard,
    onKeyboardAction: KeyboardActionHandler? = null,
    leadingIcon: (@Composable () -> Unit)? = null,
    trailingIcon: (@Composable () -> Unit)? = null,
    accept: ((CharSequence) -> Boolean)? = null,
) {
    val state = rememberTextFieldState(value)
    val latestOnChange by rememberUpdatedState(onValueChange)
    // Texts handed to the caller whose echo has not come back yet. A value
    // that matches one is that echo, however much has been typed since; any
    // other value is a change from outside, such as a cleared filter.
    val pending = remember { ArrayDeque<String>() }
    LaunchedEffect(value) {
        val echo = pending.indexOf(value)
        if (echo >= 0) {
            repeat(echo + 1) { pending.removeFirst() }
        } else if (value != state.text.toString()) {
            pending.clear()
            state.setTextAndPlaceCursorAtEnd(value)
        }
    }
    LaunchedEffect(state) {
        var last = state.text.toString()
        snapshotFlow { state.text.toString() }.collect { text ->
            if (text != last) {
                last = text
                pending.addLast(text)
                latestOnChange(text)
            }
        }
    }
    OutlinedTextField(
        state = state,
        modifier = modifier,
        enabled = enabled,
        readOnly = readOnly,
        inputTransformation = accept?.let { rule -> InputTransformation { if (!rule(asCharSequence())) revertAllChanges() } },
        label = label?.let { { Text(it) } },
        placeholder = placeholder?.let { { Text(it) } },
        supportingText = supportingText?.let { { Text(it) } },
        isError = isError,
        leadingIcon = leadingIcon,
        trailingIcon = trailingIcon,
        keyboardOptions = keyboardOptions,
        onKeyboardAction = onKeyboardAction,
        lineLimits = if (singleLine) TextFieldLineLimits.SingleLine else TextFieldLineLimits.MultiLine(minHeightInLines = minLines),
    )
}

/** Digits only, for IDs. */
val Digits: (CharSequence) -> Boolean = { text -> text.all(Char::isDigit) }

/** A whole number that may be negative, for positions. */
val WholeNumber: (CharSequence) -> Boolean = { text -> text.withIndex().all { (index, ch) -> ch.isDigit() || (index == 0 && ch == '-') } }
