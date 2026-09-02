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

int side_rank(bool mine_survives, bool theirs_survives, int partner_role) {
    if (mine_survives && !theirs_survives) return 3;
    if (theirs_survives && !mine_survives) return 0;
    const bool both_survive = mine_survives && theirs_survives;
    if (partner_role == ROLE_COVER) return both_survive ? 2 : 1;   // save ours first
    return both_survive ? 1 : 2;                                    // set theirs first
}

bool strictly_opposed(const SeatRoles& roles) {
    int partner_of[2] = {ROLE_OPPONENT, ROLE_OPPONENT};
    for (int side = 0; side < 2; ++side) {
        for (int seat = side; seat < 4; seat += 2) {
            if (roles.is_nil(seat)) partner_of[side] = roles.role[(seat + 2) & 3];
        }
    }
    int first = -1;
    for (int a = 0; a < 2; ++a) {
        for (int b = 0; b < 2; ++b) {
            const int sum = side_rank(a != 0, b != 0, partner_of[0]) +
                            side_rank(b != 0, a != 0, partner_of[1]);
            if (first < 0) {
                first = sum;
            } else if (sum != first) {
                return false;
            }
        }
    }
    return true;
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

unsigned nil_set_mask(const SeatRoles& roles) {
    unsigned mask = 0;
    for (int s = 0; s < 4; ++s) {
        if (roles.role[s] == ROLE_NIL_SET) mask |= 1u << s;
    }
    return mask;
}

bool mask_determined_by_objective(const SeatRoles& roles) {
    // Read the SHAPE rather than re-deriving the condition here, so the two
    // cannot drift apart, and so an arrangement this function has never heard
    // of is not assumed pinned.
    std::string ignored;
    switch (seat_shape(roles, ignored)) {
        case SHAPE_SINGLE_NIL:
            // One bidder.  The mask is 0 or {bidder}, which the count names.
            return true;
        case SHAPE_OPPOSING_NILS:
            // The primary is the outcome rank, and rank <-> mask is a bijection
            // on a strictly opposed arrangement -- the only opposed arrangement
            // seat_shape accepts.
            return true;
        case SHAPE_PARTNER_NILS:
            // The primary counts bids down and cannot say which one went.
            return false;
        case SHAPE_UNSUPPORTED:
        default:
            return false;
    }
}

bool mask_determined(const SeatRoles& roles, int nils_set) {
    if (mask_determined_by_objective(roles)) return true;
    // How many bids could still have gone either way, and how many of them did.
    // A bid the caller declared down is in the mask on every line and so is not
    // a degree of freedom; the live ones are.
    int live = 0;
    for (int s = 0; s < 4; ++s) {
        if (roles.role[s] == ROLE_NIL) ++live;
    }
    const int live_down = nils_set - nil_set_count(roles);
    // C(live, live_down) == 1 exactly at the ends, and the ends are the only
    // place a count names a unique subset.
    return live_down <= 0 || live_down >= live;
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

    if (nils == 2 && second_nil_at != ((nil_at + 2) & 3)) {
        // ONE BID PER SIDE.  The two remaining seats each partner a bidder, and
        // their roles say how each side breaks the tie it cannot win outright.
        for (int s = 0; s < 4; ++s) {
            if (roles.is_nil(s)) {
                if (roles.role[s] == ROLE_NIL_SET) {
                    err = "an already-broken nil on opposing sides is not supported yet (" +
                          describe_seat_roles(roles) + ")";
                    return SHAPE_UNSUPPORTED;
                }
                continue;
            }
            if (roles.role[s] != ROLE_COVER && roles.role[s] != ROLE_OPPONENT) {
                std::ostringstream os;
                os << "seat " << SEAT_CHARS[s]
                   << " partners a nil bidder, so its role must be 2 (save our own"
                      " first) or 3 (set theirs first), not "
                   << static_cast<int>(roles.role[s]) << " (" << describe_seat_roles(roles)
                   << ")";
                err = os.str();
                return SHAPE_UNSUPPORTED;
            }
        }
        // Both partners leaning the same way is a legal deal and a HARDER game:
        // the two sides then share an interest -- both would rather have both
        // bids live, or both dead, than trade -- so their rankings are not
        // reverses of each other and no single scalar describes the position.
        // Refused by name rather than answered with a scalar that does not fit.
        if (!strictly_opposed(roles)) {
            err = "both nil bidders' partners lean the same way (" +
                  describe_seat_roles(roles) +
                  "), so the two sides share an interest and the deal is not a "
                  "strictly opposed two-team game; that is not supported yet";
            return SHAPE_UNSUPPORTED;
        }
        return SHAPE_OPPOSING_NILS;
    }

    if (nils == 2) {
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
