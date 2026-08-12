#include <clapp/clapp.hpp>

#include <cstddef>
#include <cstdio>
#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace {
    /** \brief `ping` — answers `Pong`. */
    struct[[= clapp::cmd{.name = "ping", .about = "Answer Pong"}]] cmd_ping {
    };

    /** \brief `exit` — leaves the loop. */
    struct[[= clapp::cmd{.name = "exit", .about = "Leave the REPL"}]] cmd_exit {
    };

    /** \brief One line of the REPL, parsed as if it were a whole command line. */
    struct[[= clapp::cmd{.multicall = true}]] cli {
        [[= clapp::subcommand{}]] std::variant<cmd_ping, cmd_exit> command;
    };

    /**
     * \brief Split \p line on ASCII whitespace.
     *
     * \param line The raw input line.
     * \return The words, in order; empty for a blank line.
     *
     * \note Deliberately not a shell tokenizer; see the file header.
     */
    [[nodiscard]] std::vector<std::string> split_words(std::string_view line) {
        std::vector<std::string> words;
        std::size_t at = 0;
        while (at < line.size()) {
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r'))
                ++at;
            const std::size_t start = at;
            while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r')
                ++at;
            if (at > start) words.emplace_back(line.substr(start, at - start));
        }
        return words;
    }

    /**
     * \brief Write \p text to stdout and flush, the way clap's example does.
     * \param text What to write; no newline is added.
     */
    void emit(std::string_view text) {
        static_cast<void>(std::fwrite(text.data(), 1, text.size(), stdout));
        static_cast<void>(std::fflush(stdout));
    }

    /**
     * \brief Parse one line and act on it.
     *
     * \param line The line, already trimmed of its newline.
     * \return `true` when the REPL should stop.
     */
    [[nodiscard]] bool respond(std::string_view line) {
        const std::vector<std::string> words = split_words(line);
        if (words.empty()) return false;

        // The os_str values borrow `words`, which outlives this call. Naming the vector is
        // also what avoids the `raw_args` / `std::span` ambiguity a braced list would hit on
        // libc++ — see the \warning on clapp::try_parse_from.
        std::vector<clapp::os_str> argv;
        argv.reserve(words.size());
        for (const std::string &word: words) argv.emplace_back(word);

        const std::expected<cli, clapp::error> parsed =
                clapp::try_parse_from<cli>(std::span<const clapp::os_str>(argv));
        if (!parsed.has_value()) {
            // Errors go to stdout here, not stderr: in a REPL the diagnostic belongs in the
            // transcript the user is reading. `--help` arrives through the same path, which
            // is why the stream is chosen by the loop rather than by the error.
            emit(parsed.error().render().to_string());
            return false;
        }

        if (std::holds_alternative<cmd_exit>(parsed->command)) {
            emit("Exiting ...");
            return true;
        }
        emit("Pong");
        return false;
    }
} // namespace

int main() {
    std::string line;
    for (;;) {
        emit("$ ");
        if (!std::getline(std::cin, line)) break;
        if (respond(line)) break;
    }
}
