#include "nil/corpus.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace nil {
namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string field;
    for (char ch : s) {
        if (ch == sep) {
            out.push_back(trim(field));
            field.clear();
        } else {
            field += ch;
        }
    }
    out.push_back(trim(field));
    return out;
}

}  // namespace

bool load_corpus(const std::string& path, std::vector<CorpusEntry>& out, std::string& err) {
    std::ifstream in(path.c_str());
    if (!in) {
        err = "cannot open corpus file '" + path + "'";
        return false;
    }

    out.clear();
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        const std::vector<std::string> f = split(trimmed, '|');
        std::ostringstream where;
        where << path << ":" << line_no << ": ";
        if (f.size() < 10) {
            err = where.str() + "expected at least 10 '|' separated fields, got " +
                  std::to_string(f.size());
            if (f.size() == 9) {
                err += ". This looks like a corpus from before the nils_set column; "
                       "regenerate it with tools/make_corpus.py";
            }
            return false;
        }

        CorpusEntry entry;
        entry.name = f[0];
        entry.pbn = f[1];
        if (!parse_pbn(entry.pbn, entry.position.hands, err)) {
            err = where.str() + err;
            return false;
        }
        entry.position.leader = parse_seat(f[2]);
        if (entry.position.leader < 0) {
            err = where.str() + "bad leader seat";
            return false;
        }
        // The roles run clockwise from the seat the PBN names, so they cannot
        // be read without it.  A row whose PBN parsed above always has one.
        const int anchor = pbn_anchor(entry.pbn);
        if (!parse_seat_roles(f[3], anchor, entry.roles, err)) {
            err = where.str() + err;
            return false;
        }
        if (!validate_seat_roles(entry.roles, err)) {
            err = where.str() + err;
            return false;
        }
        entry.position.spades_broken = (f[4] == "1");
        entry.trick_text = f[5];
        if (!entry.trick_text.empty()) {
            int count = 0;
            if (!parse_cards(entry.trick_text, entry.position.trick, 3, count, err)) {
                err = where.str() + err;
                return false;
            }
            entry.position.trick_len = count;
        }
        if (f[6] != "max" && f[6] != "min") {
            err = where.str() + "secondary must be 'max' or 'min', got '" + f[6] + "'";
            return false;
        }
        entry.minimise_own_tricks = (f[6] == "min");
        entry.expected_nils_set = (f[7] == "?") ? -1 : std::atoi(f[7].c_str());
        entry.expected_tricks = (f[8] == "?") ? -1 : std::atoi(f[8].c_str());
        entry.expected_side_tricks = (f[9] == "?") ? -1 : std::atoi(f[9].c_str());
        if (f.size() > 10) entry.expected_pv = f[10];
        if (f.size() > 11 && !f[11].empty()) entry.provenance = f[11];

        if (!validate(entry.position, err)) {
            err = where.str() + err;
            return false;
        }
        out.push_back(entry);
    }

    if (out.empty()) {
        err = "corpus file '" + path + "' has no records";
        return false;
    }
    return true;
}

std::string corpus_repro(const CorpusEntry& entry, const std::string& exe) {
    std::ostringstream os;
    os << exe << " --pbn '" << entry.pbn << "'"
       << " --leader " << SEAT_CHARS[entry.position.leader] << " --seats "
       << seat_roles_to_string(entry.roles, pbn_anchor(entry.pbn));
    if (entry.position.spades_broken) os << " --spades-broken";
    if (entry.minimise_own_tricks) os << " --secondary min";
    if (!entry.trick_text.empty()) os << " --trick '" << entry.trick_text << "'";
    return os.str();
}

}  // namespace nil
