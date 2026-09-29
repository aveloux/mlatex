/// @file
/// @brief Cursor implementation: the reversed token stack.
///
/// The vector is held back-to-front so that the next token is the last live
/// one and injecting an expansion result is a write at the end. Every
/// operation here is O(1) or O(k) in what is being injected, never O(n) in
/// the stream.
///
/// The live part of the vector is its first `depth` entries rather than all of
/// it. Reading a token lowers the depth and leaves the entry where it was;
/// injecting cuts the vector back to the depth before writing past it. That
/// is what spares advance() -- which runs once per token of every document --
/// from shrinking the vector, which is the one step of it that is not free.
#include "syntax/cursor.hpp"
#include "logger.hpp"

#include <algorithm>
#include <utility>

namespace syntax {

    Cursor::Cursor(std::vector<Token> stream) {
        if (!stream.empty()) {
            this->tokens = std::move(stream);
            std::ranges::reverse(this->tokens);
            this->depth = this->tokens.size();
            Logger::log(Logger::Type::Mouth, Logger::Level::Debug,
                        "Cursor stream initialized (size={})", this->depth);
        }
    }

    Token Cursor::lookahead(const std::size_t offset) const noexcept {
        if (offset < this->depth) {
            return this->tokens.data()[this->depth - 1 - offset];
        }
        return {};
    }

    Token Cursor::advance() noexcept {
        if (this->depth == 0) return {};

        const Token token = this->tokens.data()[--this->depth];
        this->served++;
        if (this->depth < this->unread) this->unread = this->depth;
        Logger::log(Logger::Type::Mouth, Logger::Level::Traceback,
                    "Cursor -> [pending={}, text='{}']", this->depth, token.text);
        return token;
    }

    void Cursor::inject(const std::span<const Token> batch) {
        if (batch.empty()) return;

        const Token* first = this->tokens.data();
        const Token* last = first + this->tokens.size();

        // Guard against a caller handing us a span into our own storage --
        // spent entries included, since those are about to be cut away:
        // insert() may reallocate partway through and leave it dangling.
        if (batch.data() >= first && batch.data() < last) {
            const std::vector<Token> copy(batch.begin(), batch.end());
            this->tokens.resize(this->depth);
            this->tokens.insert(this->tokens.end(), copy.rbegin(), copy.rend());
        } else {
            this->tokens.resize(this->depth);
            this->tokens.insert(this->tokens.end(), batch.rbegin(), batch.rend());
        }
        this->depth = this->tokens.size();

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug,
                    "Cursor injected {} token(s), {} now pending", batch.size(), this->depth);
    }

    void Cursor::adopt(std::vector<Token>&& incoming) {
        if (incoming.empty()) return;

        if (this->depth == 0) {
            this->tokens = std::move(incoming);
            std::ranges::reverse(this->tokens);
            this->depth = this->tokens.size();
            this->unread = this->depth;
            Logger::log(Logger::Type::Mouth, Logger::Level::Debug,
                        "Cursor adopted {} token(s)", this->depth);
            return;
        }

        this->inject(incoming);
    }

    std::vector<Token> Cursor::source() const {
        return {this->tokens.rend() - static_cast<std::ptrdiff_t>(this->unread), this->tokens.rend()};
    }

    void Cursor::source(const std::vector<Token>& replacement) {
        // Beneath what was injected over it, and held reversed as the rest is.
        std::vector<Token> rebuilt(replacement.rbegin(), replacement.rend());
        rebuilt.insert(rebuilt.end(), this->tokens.begin() + static_cast<std::ptrdiff_t>(this->unread),
                       this->tokens.begin() + static_cast<std::ptrdiff_t>(this->depth));
        this->tokens = std::move(rebuilt);
        this->depth = this->tokens.size();
        this->unread = replacement.size();
    }

    bool Cursor::empty() const noexcept {
        return this->depth == 0;
    }

    std::size_t Cursor::size() const noexcept {
        return this->depth;
    }

    std::size_t Cursor::consumed() const noexcept {
        return this->served;
    }

    void Cursor::dispose() noexcept {
        this->tokens.clear();
        this->depth = 0;
        this->unread = 0;
    }

}
