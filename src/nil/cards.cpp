#include "nil/cards.hpp"

#include <cctype>
#include <cstring>
#include <vector>

namespace nil {

std::string card_to_string(CardId c) {
    std::string s;
    s += SUIT_CHARS[card_suit(c)];
    s += RANK_CHARS[card_rank(c) - 2];
    return s;
}

std::string hand_to_string(Hand h) {
    std::string out;
    while (h) {
        if (!out.empty()) out += ' ';
        out += card_to_string(take_lowest(h));
    }
    return out;
}

bool parse_card(const std::string& text, CardId& out, std::string& err) {
    std::string t;
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch)))
            t += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    // Accept "10" as a synonym for "T", the way the oracle does.
    std::string::size_type pos;
    while ((pos = t.find("10")) != std::string::npos) t.replace(pos, 2, "T");

    if (t.size() != 2) {
        err = "bad card '" + text + "'";
        return false;
    }
    const char* sp = std::strchr(SUIT_CHARS, t[0]);
    const char* rp = std::strchr(RANK_CHARS, t[1]);
    if (!sp || !rp || t[0] == '\0' || t[1] == '\0') {
        err = "bad card '" + text + "'";
        return false;
    }
    out = make_card(static_cast<int>(sp - SUIT_CHARS),
                    static_cast<int>(rp - RANK_CHARS) + 2);
    return true;
}

bool parse_cards(const std::string& text, CardId* out, int max_out, int& count,
                 std::string& err) {
    count = 0;
    std::string tok;
    std::vector<std::string> tokens;
    for (char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',') {
            if (!tok.empty()) tokens.push_back(tok);
            tok.clear();
        } else {
            tok += ch;
        }
    }
    if (!tok.empty()) tokens.push_back(tok);

    for (const std::string& t : tokens) {
        if (count >= max_out) {
            err = "too many cards in '" + text + "'";
            return false;
        }
        if (!parse_card(t, out[count], err)) return false;
        ++count;
    }
    return true;
}

int parse_seat(const std::string& text) {
    for (char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) continue;
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const char* p = std::strchr(SEAT_CHARS, up);
        return (p && up != '\0') ? static_cast<int>(p - SEAT_CHARS) : -1;
    }
    return -1;
}

}  // namespace nil
