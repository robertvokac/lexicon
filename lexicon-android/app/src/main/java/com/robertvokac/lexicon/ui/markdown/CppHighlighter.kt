package com.robertvokac.lexicon.ui.markdown

/**
 * C++ highlighting for code blocks marked cpp, the counterpart of the desktop
 * CodeHighlighter and lexicon-web/js/highlight.js: keywords, strings,
 * comments and preprocessor directives. Joined back together the token texts
 * are exactly the source.
 */
object CppHighlighter {
    enum class Kind { Keyword, StringLiteral, Comment, Preprocessor }

    data class Token(val kind: Kind?, val text: String)

    private val languages = setOf("cpp", "c++", "cxx", "cc", "hpp", "hxx", "h")

    // The C++23 keywords, the alternative operator spellings, the identifiers
    // with a special meaning, and Qt's signals and slots as on the desktop.
    private val keywords = """
        alignas alignof and and_eq asm auto bitand bitor bool break case catch char
        char8_t char16_t char32_t class compl concept const consteval constexpr
        constinit const_cast continue co_await co_return co_yield decltype default
        delete do double dynamic_cast else enum explicit export extern false float
        for friend goto if inline int long mutable namespace new noexcept not not_eq
        nullptr operator or or_eq private protected public register reinterpret_cast
        requires return short signed sizeof static static_assert static_cast struct
        switch template this thread_local throw true try typedef typeid typename
        union unsigned using virtual void volatile wchar_t while xor xor_eq
        final override import module signals slots
    """.trim().split(Regex("\\s+")).toSet()

    private val literalPrefixes = setOf("L", "u", "U", "u8")
    private val rawPrefixes = setOf("R", "LR", "uR", "UR", "u8R")
    private val directive = Regex("^#\\s*[A-Za-z_]\\w*")
    private val includeHeader = Regex("^([ \\t]*)(<[^>\\n]*>)")

    fun handles(language: String): Boolean = language.lowercase() in languages

    fun tokenize(source: String): List<Token> {
        val tokens = mutableListOf<Token>()
        val plain = StringBuilder()
        fun emit(kind: Kind, text: String) {
            if (plain.isNotEmpty()) tokens += Token(null, plain.toString())
            plain.setLength(0)
            tokens += Token(kind, text)
        }
        // Where a quoted literal starting at [from] ends: the closing quote,
        // or the end of the line for one left open.
        fun quotedEnd(from: Int): Int {
            val quote = source[from]
            var index = from + 1
            while (index < source.length && source[index] != quote && source[index] != '\n') {
                index += if (source[index] == '\\') 2 else 1
            }
            return minOf(if (index < source.length && source[index] == quote) index + 1 else index, source.length)
        }
        var index = 0
        var lineStart = true
        while (index < source.length) {
            val ch = source[index]
            val next = source.getOrNull(index + 1)
            if (ch == '/' && next == '/') {
                val end = source.indexOf('\n', index).let { if (it < 0) source.length else it }
                emit(Kind.Comment, source.substring(index, end))
                index = end
                continue
            }
            if (ch == '/' && next == '*') {
                val end = source.indexOf("*/", index + 2).let { if (it < 0) source.length else it + 2 }
                emit(Kind.Comment, source.substring(index, end))
                index = end
                lineStart = false
                continue
            }
            if (ch == '#' && lineStart) {
                val match = directive.find(source.substring(index))
                if (match != null) {
                    emit(Kind.Preprocessor, match.value)
                    index += match.value.length
                    lineStart = false
                    // The <header> of an #include reads as a string, as "a.h" does.
                    if (match.value.endsWith("include")) {
                        includeHeader.find(source.substring(index))?.let { header ->
                            plain.append(header.groupValues[1])
                            emit(Kind.StringLiteral, header.groupValues[2])
                            index += header.value.length
                        }
                    }
                    continue
                }
            }
            if (ch == '"' || ch == '\'') {
                val end = quotedEnd(index)
                emit(Kind.StringLiteral, source.substring(index, end))
                index = end
                lineStart = false
                continue
            }
            if (ch.isLetter() && ch.code < 128 || ch == '_') {
                var end = index + 1
                while (end < source.length && (source[end].isLetterOrDigit() && source[end].code < 128 || source[end] == '_')) end++
                val word = source.substring(index, end)
                val following = source.getOrNull(end)
                when {
                    following == '"' && word in rawPrefixes -> {
                        // R"delimiter( ... )delimiter"
                        val open = source.indexOf('(', end)
                        val delimiter = if (open < 0) "" else source.substring(end + 1, open)
                        val close = if (open < 0) -1 else source.indexOf(")$delimiter\"", open)
                        val stop = if (close < 0) source.length else close + delimiter.length + 2
                        emit(Kind.StringLiteral, source.substring(index, stop))
                        index = stop
                    }
                    (following == '"' || following == '\'') && word in literalPrefixes -> {
                        val stop = quotedEnd(end)
                        emit(Kind.StringLiteral, source.substring(index, stop))
                        index = stop
                    }
                    word in keywords -> {
                        emit(Kind.Keyword, word)
                        index = end
                    }
                    else -> {
                        plain.append(word)
                        index = end
                    }
                }
                lineStart = false
                continue
            }
            if (ch in '0'..'9') {
                // Includes the ' separators of 1'000'000, which must not open
                // a character literal.
                var end = index + 1
                while (end < source.length && (source[end].isLetterOrDigit() && source[end].code < 128 || source[end] in "_.'")) end++
                plain.append(source, index, end)
                index = end
                lineStart = false
                continue
            }
            plain.append(ch)
            if (ch == '\n') lineStart = true else if (ch != ' ' && ch != '\t' && ch != '\r') lineStart = false
            index++
        }
        if (plain.isNotEmpty()) tokens += Token(null, plain.toString())
        return tokens
    }
}
