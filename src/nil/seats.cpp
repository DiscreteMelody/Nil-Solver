#include "nil/seats.hpp"

#include <cctype>
#include <sstream>

namespace nil {

const char* role_name(int role) {
    switch (role) {
        case ROLE_NIL: return "nil";
        case ROLE_NIL_SET: return "nil-set";
        case ROLE_COVER: return "cover";
        case ROLE_OPPONENT: return "opponent";
        default: return "?";
    }
}

int SeatRoles::nil_seat() const {
    for (int s = 0; s < 4; ++s) {
        if (is_nil(s)) return s;
    }
    return -1;
}

int SeatRoles::cover_seat() const {
    for (int s = 0; s < 4; ++s) {
        if (role[s] == ROLE_COVER) return s;
    }
    return -1;
}

bool SeatRoles::nil_already_set() const {
    for (int s = 0; s < 4; ++s) {
        if (role[s] == ROLE_NIL_SET) return true;
    }
    return false;
}

SeatRoles seat_roles_from_nil(int nil_seat, bool already_set) {
    SeatRoles out;
    for (int s = 0; s < 4; ++s) out.role[s] = ROLE_OPPONENT;
    out.role[nil_seat & 3] = already_set ? ROLE_NIL_SET : ROLE_NIL;
    out.role[(nil_seat + 2) & 3] = ROLE_COVER;
    return out;
}

bool validate_seat_roles(const SeatRoles& roles, std::string& err) {
    int nils = 0;
    int covers = 0;
    int nil_at = -1;
    int cover_at = -1;
    for (int s = 0; s < 4; ++s) {
        const int r = static_cast<int>(roles.role[s]);
        if (r < ROLE_NIL || r > ROLE_OPPONENT) {
            std::ostringstream os;
            os << "seat " << SEAT_CHARS[s] << " has role " << r
               << "; expected 0 (nil), 1 (nil-set), 2 (cover) or 3 (opponent)";
            err = os.str();
            return false;
        }
        if (roles.is_nil(s)) {
            ++nils;
            if (nil_at < 0) nil_at = s;
        }
        if (r == ROLE_COVER) {
            ++covers;
            if (cover_at < 0) cover_at = s;
        }
    }

    if (nils == 0) {
        err = "no seat bid nil (" + describe_seat_roles(roles) +
              "); one seat must have role 0 or 1";
        return false;
    }
    // The one refusal that is about a feature rather than about a mistake.  Say
    // so plainly: a caller passing two nils has described a legal spades deal,
    // and the solver is the thing that is not ready.
    if (nils > 1) {
        err = "more than one nil bid (" + describe_seat_roles(roles) +
              "); solving with multiple nils is not supported yet";
        return false;
    }
    if (covers == 0) {
        err = "no cover partner for " + std::string(1, SEAT_CHARS[nil_at]) + "'s nil (" +
              describe_seat_roles(roles) + "); one seat must have role 2";
        return false;
    }
    if (covers > 1) {
        err = "more than one cover partner (" + describe_seat_roles(roles) +
              "); exactly one seat may have role 2";
        return false;
    }
    if (cover_at != ((nil_at + 2) & 3)) {
        std::ostringstream os;
        os << "the cover partner must sit across from the nil bidder: " << SEAT_CHARS[nil_at]
           << " bid nil, so the cover is " << SEAT_CHARS[(nil_at + 2) & 3] << " and not "
           << SEAT_CHARS[cover_at] << " (" << describe_seat_roles(roles) << ")";
        err = os.str();
        return false;
    }
    return true;
}

bool parse_seat_roles(const std::string& text, int anchor, SeatRoles& out, std::string& err) {
    if (anchor < 0 || anchor > 3) {
        err = "seat roles need an anchor seat to be read against";
        return false;
    }

    // Digits, in the order written.  Whitespace and commas separate; four
    // digits run together are four values, which is what a corpus column or a
    // single shell word tends to look like.
    int values[5] = {0, 0, 0, 0, 0};
    int count = 0;
    for (char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',') continue;
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            err = std::string("bad seat role character '") + ch + "' in '" + text +
                  "'; expected four values from 0 (nil), 1 (nil-set), 2 (cover), "
                  "3 (opponent)";
            return false;
        }
        if (count >= 4) {
            err = "too many seat roles in '" + text + "'; expected exactly four";
            return false;
        }
        values[count++] = ch - '0';
    }
    if (count != 4) {
        std::ostringstream os;
        os << "expected exactly four seat roles, got " << count << " in '" << text << "'";
        err = os.str();
        return false;
    }

    for (int offset = 0; offset < 4; ++offset) {
        const int v = values[offset];
        if (v < ROLE_NIL || v > ROLE_OPPONENT) {
            std::ostringstream os;
            os << "seat role " << v << " out of range in '" << text
               << "'; expected 0 (nil), 1 (nil-set), 2 (cover) or 3 (opponent)";
            err = os.str();
            return false;
        }
        out.role[(anchor + offset) & 3] = static_cast<SeatRole>(v);
    }
    return true;
}

std::string seat_roles_to_string(const SeatRoles& roles, int anchor) {
    std::string out;
    for (int offset = 0; offset < 4; ++offset) {
        if (offset) out += ' ';
        out += static_cast<char>('0' + static_cast<int>(roles.role[(anchor + offset) & 3]));
    }
    return out;
}

std::string describe_seat_roles(const SeatRoles& roles) {
    std::string out;
    for (int s = 0; s < 4; ++s) {
        if (s) out += ' ';
        out += SEAT_CHARS[s];
        out += '=';
        out += role_name(roles.role[s]);
    }
    return out;
}

}  // namespace nil
