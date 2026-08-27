#include "nil/seats.hpp"

#include <cctype>
#include <sstream>
#include <string>

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

int nil_count(const SeatRoles& roles) {
    int n = 0;
    for (int s = 0; s < 4; ++s) {
        if (roles.is_nil(s)) ++n;
    }
    return n;
}

int nil_set_count(const SeatRoles& roles) {
    int n = 0;
    for (int s = 0; s < 4; ++s) {
        if (roles.role[s] == ROLE_NIL_SET) ++n;
    }
    return n;
}

unsigned live_nil_mask(const SeatRoles& roles) {
    unsigned mask = 0;
    for (int s = 0; s < 4; ++s) {
        if (roles.role[s] == ROLE_NIL) mask |= 1u << s;
    }
    return mask;
}

bool validate_seat_roles(const SeatRoles& roles, std::string& err) {
    return seat_shape(roles, err) != SHAPE_UNSUPPORTED;
}

SeatShape seat_shape(const SeatRoles& roles, std::string& err) {
    int nils = 0;
    int covers = 0;
    int nil_at = -1;
    int second_nil_at = -1;
    int cover_at = -1;
    for (int s = 0; s < 4; ++s) {
        const int r = static_cast<int>(roles.role[s]);
        if (r < ROLE_NIL || r > ROLE_OPPONENT) {
            std::ostringstream os;
            os << "seat " << SEAT_CHARS[s] << " has role " << r
               << "; expected 0 (nil), 1 (nil-set), 2 (cover) or 3 (opponent)";
            err = os.str();
            return SHAPE_UNSUPPORTED;
        }
        if (roles.is_nil(s)) {
            ++nils;
            if (nil_at < 0) {
                nil_at = s;
            } else if (second_nil_at < 0) {
                second_nil_at = s;
            }
        }
        if (r == ROLE_COVER) {
            ++covers;
            if (cover_at < 0) cover_at = s;
        }
    }

    if (nils == 0) {
        err = "no seat bid nil (" + describe_seat_roles(roles) +
              "); one seat must have role 0 or 1";
        return SHAPE_UNSUPPORTED;
    }

    if (nils == 1) {
        if (covers == 0) {
            err = "no cover partner for " + std::string(1, SEAT_CHARS[nil_at]) + "'s nil (" +
                  describe_seat_roles(roles) + "); one seat must have role 2";
            return SHAPE_UNSUPPORTED;
        }
        if (covers > 1) {
            err = "more than one cover partner (" + describe_seat_roles(roles) +
                  "); exactly one seat may have role 2";
            return SHAPE_UNSUPPORTED;
        }
        if (cover_at != ((nil_at + 2) & 3)) {
            std::ostringstream os;
            os << "the cover partner must sit across from the nil bidder: " << SEAT_CHARS[nil_at]
               << " bid nil, so the cover is " << SEAT_CHARS[(nil_at + 2) & 3] << " and not "
               << SEAT_CHARS[cover_at] << " (" << describe_seat_roles(roles) << ")";
            err = os.str();
            return SHAPE_UNSUPPORTED;
        }
        return SHAPE_SINGLE_NIL;
    }

    if (nils == 2) {
        if (second_nil_at != ((nil_at + 2) & 3)) {
            err = "the two nil bidders are not partners (" + describe_seat_roles(roles) +
                  "); nils on OPPOSING sides are not supported yet";
            return SHAPE_UNSUPPORTED;
        }
        if (covers > 0) {
            err = "a pair that both bid nil has nobody left to cover (" +
                  describe_seat_roles(roles) + "); the other two seats must both have role 3";
            return SHAPE_UNSUPPORTED;
        }
        // A bid already down is a fact about the deal, not an unimplemented
        // shape: it is the state a real hand reaches the moment one of two nils
        // breaks, and re-solving from it is the point.
        //
        // BOTH DOWN IS ACCEPTED TOO, and degenerates rather than failing.  With
        // no live bid there is no primary level, so what is left is the
        // secondary alone: each pair takes as many tricks as it can, or sheds as
        // many as it can, according to `secondary`.  That is an ordinary
        // double-dummy question and a perfectly good one to ask -- the hand goes
        // on being played, and the trick count is still worth points.  It is
        // also exactly what a SINGLE nil already set has always done, so
        // refusing it here was an inconsistency rather than a safeguard.
        return SHAPE_PARTNER_NILS;
    }

    err = std::to_string(nils) + " nils (" + describe_seat_roles(roles) +
          "); more than two is not supported yet";
    return SHAPE_UNSUPPORTED;
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
