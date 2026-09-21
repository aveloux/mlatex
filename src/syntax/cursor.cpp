/// @file
/// @brief Cursor implementation: the reversed token stack.
///
/// The vector is held back-to-front so that the next token is back() and
/// injecting an expansion result is a push at the end. Every operation here is
/// O(1) or O(k) in what is being injected, never O(n) in the stream.
#include "syntax/cursor.hpp"
#include "logger.hpp"

#include <algorithm>
#include <utility>

namespace syntax {

    Cursor::Cursor(std::vector<Token> stream) {
        if (!stream.empty()) {
            this->tokens = std::move(stream);
            std::ranges::reverse(this->tokens);
            Logger::fmt(Logger::Type::Mouth, Logger::Level::Debug,
                        "Cursor stream initialized (size={})", this->tokens.size());
        }
    }

    Token Cursor::lookahead(const std::size_t offset) const noexcept {
        if (offset < this->tokens.size()) {
            return this->tokens[this->tokens.size() - 1 - offset];
        }
        return {};
    }

    Token Cursor::advance() noexcept {
        if (this->tokens.empty()) return {};

        Token token = this->tokens.back();
        this->tokens.pop_back();
        this->served++;
        Logger::fmt(Logger::Type::Mouth, Logger::Level::Traceback,
                    "Cursor -> [pending={}, text='{}']", this->tokens.size(), token.values);
        return token;
    }

    void Cursor::inject(const std::span<const Token> batch) {
        if (batch.empty()) return;

        const Token* first = this->tokens.data();
        const Token* last = first + this->tokens.size();

        // Guard against a caller handing us a span into our own storage:
        // insert() may reallocate partway through and leave it dangling.
        if (batch.data() >= first && batch.data() < last) {
            const std::vector<Token> copy(batch.begin(), batch.end());
            this->tokens.insert(this->tokens.end(), copy.rbegin(), copy.rend());
        } else {
            this->tokens.insert(this->tokens.end(), batch.rbegin(), batch.rend());
        }

        Logger::fmt(Logger::Type::Mouth, Logger::Level::Debug,
                    "Cursor injected {} token(s), {} now pending", batch.size(), this->tokens.size());
    }

    void Cursor::adopt(std::vector<Token>&& incoming) {
        if (incoming.empty()) return;

        if (this->tokens.empty()) {
            this->tokens = std::move(incoming);
            std::ranges::reverse(this->tokens);
            Logger::fmt(Logger::Type::Mouth, Logger::Level::Debug,
                        "Cursor adopted {} token(s)", this->tokens.size());
            return;
        }

        this->inject(incoming);
    }

    bool Cursor::empty() const noexcept {
        return this->tokens.empty();
    }

    std::size_t Cursor::size() const noexcept {
        return this->tokens.size();
    }

    std::size_t Cursor::consumed() const noexcept {
        return this->served;
    }

    void Cursor::clear() noexcept {
        this->tokens.clear();
    }

    void Cursor::reserve(const std::size_t capacity) {
        this->tokens.reserve(capacity);
    }

}