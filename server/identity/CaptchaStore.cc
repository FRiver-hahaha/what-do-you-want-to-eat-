#include "server/identity/CaptchaStore.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <random>
#include <sstream>
#include <string>

namespace eat::identity {
namespace {

constexpr char kChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr std::size_t kCodeLength = 4;

std::string HexId(std::mt19937_64& rng) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(16);
    for (int i = 0; i < 16; ++i) {
        out.push_back(kHex[rng() & 0xF]);
    }
    return out;
}

std::string BuildSvg(const std::string& code, std::mt19937_64& rng) {
    static const char* kInk[] = {"#24324a", "#2f6f5e", "#6b4f2a", "#4b3f72"};
    static const char* kLine[] = {"#9db4c8", "#c3b18d", "#9cc7b8"};

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"140\" height=\"46\" "
           "viewBox=\"0 0 140 46\" role=\"img\" aria-label=\"captcha\">";
    svg << "<rect width=\"140\" height=\"46\" rx=\"10\" fill=\"#f3f6fb\"/>";

    for (int i = 0; i < 3; ++i) {
        const int x1 = static_cast<int>(rng() % 60);
        const int y1 = static_cast<int>(rng() % 46);
        const int x2 = 80 + static_cast<int>(rng() % 60);
        const int y2 = static_cast<int>(rng() % 46);
        svg << "<path d=\"M" << x1 << " " << y1 << " Q70 " << (rng() % 46) << " " << x2
            << " " << y2 << "\" fill=\"none\" stroke=\"" << kLine[rng() % 3]
            << "\" stroke-width=\"1.2\" opacity=\"0.75\"/>";
    }

    for (std::size_t i = 0; i < code.size(); ++i) {
        const int x = 16 + static_cast<int>(i) * 28;
        const int y = 31 + static_cast<int>(rng() % 5) - 2;
        const int angle = static_cast<int>(rng() % 25) - 12;
        svg << "<text x=\"" << x << "\" y=\"" << y << "\" font-family=\"ui-monospace, "
               "SFMono-Regular, Menlo, monospace\" font-size=\"25\" font-weight=\"700\" "
               "fill=\""
            << kInk[rng() % 4] << "\" transform=\"rotate(" << angle << " " << x << " "
            << (y - 8) << ")\">" << code[i] << "</text>";
    }

    for (int i = 0; i < 18; ++i) {
        svg << "<circle cx=\"" << (rng() % 140) << "\" cy=\"" << (rng() % 46)
            << "\" r=\"0.9\" fill=\"#b9c6d6\" opacity=\"0.6\"/>";
    }
    svg << "</svg>";
    return svg.str();
}

std::string UpperAscii(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

}  // namespace

CaptchaStore::CaptchaStore(std::uint32_t ttl_s) : ttl_s_(ttl_s == 0 ? 120 : ttl_s) {}

CaptchaStore::Challenge CaptchaStore::Issue() {
    std::random_device device;
    std::mt19937_64 rng(device() ^ static_cast<std::uint64_t>(
                                        std::chrono::steady_clock::now().time_since_epoch().count()));

    std::string code;
    code.reserve(kCodeLength);
    for (std::size_t i = 0; i < kCodeLength; ++i) {
        code.push_back(kChars[rng() % (sizeof(kChars) - 1)]);
    }

    Challenge challenge;
    challenge.id = HexId(rng);
    challenge.svg = BuildSvg(code, rng);
    challenge.expires_in_s = ttl_s_;

    std::lock_guard<std::mutex> lock(mutex_);
    PruneLocked();
    if (entries_.size() >= max_entries_) {
        entries_.clear();
    }
    entries_[challenge.id] = Entry{code, std::chrono::steady_clock::now() +
                                            std::chrono::seconds(ttl_s_)};
    return challenge;
}

bool CaptchaStore::Verify(const std::string& id, const std::string& code) {
    if (id.empty() || code.empty()) return false;
    const std::string normalized = UpperAscii(code);

    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    const Entry entry = found->second;
    entries_.erase(found);
    if (std::chrono::steady_clock::now() >= entry.expires) return false;
    return entry.code == normalized;
}

void CaptchaStore::PruneLocked() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now >= it->second.expires) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace eat::identity
