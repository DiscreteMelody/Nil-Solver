#include "nil/position.hpp"

#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace nil {

int Position::cards_per_hand() const {
    int best = 0;
    for (int s = 0; s < 4; ++s) {
        const int n = count_cards(hands[s]);
        if (n > best) best = n;
    }
    return best;
}

bool parse_pbn_hand(const std::string& text, Hand& out, std::string& err) {
    out = 0;

    std::string t;
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) t += ch;
    }
    if (t.empty() || t == "-") return true;  // PBN's "not given" / empty hand

    std::string::size_type pos;
    while ((pos = t.find("10")) != std::string::npos) t.replace(pos, 2, "T");

    std::vector<std::string> parts;
    std::string part;
    for (char ch : t) {
        if (ch == '.') {
            parts.push_back(part);
            part.clear();
        } else {
            part += ch;
        }
    }
    parts.push_back(part);

    if (parts.size() != 4) {
        std::ostringstream os;
        os << "hand '" << text << "' has " << parts.size()
           << " suit group(s); expected 4 (spades.hearts.diamonds.clubs)";
        err = os.str();
        return false;
    }

    for (int suit = 0; suit < 4; ++suit) {
        for (char ch : parts[static_cast<std::size_t>(suit)]) {
            if (ch == '-') continue;  // some writers use '-' for a void
            const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const char* rp = std::strchr(RANK_CHARS, up);
            if (!rp || up == '\0') {
                err = std::string("bad rank '") + ch + "' in hand '" + text + "'";
                return false;
            }
            const CardId card =
                make_card(suit, static_cast<int>(rp - RANK_CHARS) + 2);
            if (out & card_bit(card)) {
                err = "hand '" + text + "' repeats " + card_to_string(card);
                return false;
            }
            out |= card_bit(card);
        }
    }
    return true;
}

int pbn_anchor(const std::string& text) {
    std::string t = text;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    if (t.size() < 2 || t[1] != ':') return -1;
    return parse_seat(t.substr(0, 1));
}

bool parse_pbn(const std::string& text, Hand hands[4], std::string& err) {
    for (int s = 0; s < 4; ++s) hands[s] = 0;

    std::string t = text;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();

    if (t.size() < 2 || t[1] != ':') {
        err = "PBN deal must start with a seat letter and a colon, e.g. 'N:'";
        return false;
    }
    const int first = parse_seat(t.substr(0, 1));
    if (first < 0) {
        err = std::string("bad seat '") + t[0] + "' (expected one of N, E, S, W)";
        return false;
    }

    std::vector<std::string> groups;
    std::string group;
    for (std::string::size_type i = 2; i < t.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(t[i]))) {
            if (!group.empty()) groups.push_back(group);
            group.clear();
        } else {
            group += t[i];
        }
    }
    if (!group.empty()) groups.push_back(group);

    if (groups.size() != 4) {
        std::ostringstream os;
        os << "expected 4 hands, got " << groups.size();
        err = os.str();
        return false;
    }

    for (int offset = 0; offset < 4; ++offset) {
        Hand h = 0;
        if (!parse_pbn_hand(groups[static_cast<std::size_t>(offset)], h, err)) return false;
        hands[(first + offset) & 3] = h;
    }
    return true;
}

std::string deal_to_pbn(const Hand hands[4], int first_seat) {
    std::ostringstream os;
    os << SEAT_CHARS[first_seat] << ':';
    for (int offset = 0; offset < 4; ++offset) {
        const Hand hand = hands[(first_seat + offset) & 3];
        if (offset) os << ' ';
        for (int suit = 0; suit < 4; ++suit) {
            if (suit) os << '.';
            for (int rank = 14; rank >= 2; --rank) {
                if (hand & card_bit(make_card(suit, rank))) os << RANK_CHARS[rank - 2];
            }
        }
    }
    return os.str();
}

bool validate(const Position& pos, std::string& err) {
    if (pos.leader < 0 || pos.leader > 3) {
        err = "leader out of range";
        return false;
    }
    if (pos.trick_len < 0 || pos.trick_len > 3) {
        err = "current trick must hold 0-3 cards";
        return false;
    }

    int owner[64];
    for (int i = 0; i < 64; ++i) owner[i] = -1;  // -1 unseen, 0..3 seat, 4 trick

    for (int seat = 0; seat < 4; ++seat) {
        Hand h = pos.hands[seat];
        while (h) {
            const CardId c = take_lowest(h);
            if (owner[c] >= 0) {
                err = "duplicate card " + card_to_string(c) + " in " +
                      SEAT_CHARS[seat] + " and " + SEAT_CHARS[owner[c]];
                return false;
            }
            owner[c] = seat;
        }
    }
    for (int i = 0; i < pos.trick_len; ++i) {
        const CardId c = pos.trick[i];
        if (owner[c] >= 0 && owner[c] < 4) {
            err = "card " + card_to_string(c) + " is both on the trick and in " +
                  SEAT_CHARS[owner[c]];
            return false;
        }
        if (owner[c] == 4) {
            err = "card " + card_to_string(c) + " appears twice on the trick";
            return false;
        }
        owner[c] = 4;
    }

    // Everyone held the same number of cards at the start of this trick.
    const int played = pos.trick_len;
    const int base = count_cards(pos.hands[(pos.leader + played) & 3]);
    for (int offset = 0; offset < 4; ++offset) {
        const int seat = (pos.leader + offset) & 3;
        const int want = (offset < played) ? base - 1 : base;
        if (count_cards(pos.hands[seat]) != want) {
            std::ostringstream os;
            os << "inconsistent hand sizes: ";
            for (int s = 0; s < 4; ++s) {
                if (s) os << ", ";
                os << SEAT_CHARS[s] << '=' << count_cards(pos.hands[s]);
            }
            os << " with " << played << " card(s) already on the trick";
            err = os.str();
            return false;
        }
    }

    // The cards already on the trick must be consistent with follow-suit given
    // what their players still hold.  (We do not re-derive earlier history.)
    if (played) {
        const int led = card_suit(pos.trick[0]);
        for (int offset = 1; offset < played; ++offset) {
            const int seat = (pos.leader + offset) & 3;
            const CardId card = pos.trick[offset];
            if (card_suit(card) != led && (pos.hands[seat] & suit_mask(led))) {
                err = std::string(1, SEAT_CHARS[seat]) + " played " +
                      card_to_string(card) + " off-suit while still holding " +
                      SUIT_CHARS[led];
                return false;
            }
        }
    }
    // Spades cannot be broken while every spade is still IN A HAND.
    //
    // Breaking spades means one has been played, and a spade that has been
    // played is one that has left a hand.  So if all thirteen are still held,
    // the flag is claiming something that cannot have happened, and a full
    // 13-card deal is exactly that case: no card has been played at all.
    //
    // A SPADE ON THE CURRENT TRICK HAS LEFT A HAND, so it does not count here.
    // The earlier version added trick spades to this total, which rejected a
    // reachable position: lead a diamond, have it ruffed, and twelve spades sit
    // in hands with the thirteenth face up on the table -- spades are broken by
    // that ruff, and the count still read thirteen.  Roughly 8 in 3,000
    // generated twelve-card layouts with a partial trick land on it.
    //
    // Below thirteen the count says nothing about a CONSTRUCTED ending, because
    // there the absent spades were never dealt rather than played, so such a
    // position may legitimately start either way.
    //
    // This is worth rejecting rather than tolerating.  Such a position is not
    // merely unreachable, it is EXPENSIVE: unbroken spades forbid a voluntary
    // spade lead, which prunes hard near the root, and setting the flag on a
    // full deal throws that away and searches a game nobody can play.
    {
        int spades_held = 0;
        for (int seat = 0; seat < 4; ++seat)
            spades_held += count_cards(pos.hands[seat] & suit_mask(SUIT_SPADES));
        if (pos.spades_broken && spades_held == 13) {
            err = "spades cannot be broken: all thirteen are still in hand, so "
                  "none has been played";
            return false;
        }
    }

    return true;
}

std::string format_hands(const Position& pos) {
    std::ostringstream os;
    for (int seat = 0; seat < 4; ++seat) {
        if (seat) os << '\n';
        os << "  " << SEAT_CHARS[seat] << "  ";
        for (int suit = 0; suit < 4; ++suit) {
            if (suit) os << ' ';
            os << SUIT_CHARS[suit] << ':';
            std::string ranks;
            for (int rank = 14; rank >= 2; --rank) {
                if (pos.hands[seat] & card_bit(make_card(suit, rank)))
                    ranks += RANK_CHARS[rank - 2];
            }
            os << (ranks.empty() ? "-" : ranks);
        }
    }
    return os.str();
}

}  // namespace nil
