#include "tldregistry/tldregistry.h"

#include "net/base/lookup_string_in_fixed_set.h"

bool tldregistry::is_tld(std::string_view domain) {
#include "effective_tld_names.inc"
    return net::kDafsaFound == net::LookupStringInFixedSet(&kDafsa[0], sizeof(kDafsa), domain.data(), domain.size());
}

std::optional<std::string_view> tldregistry::get_tld(std::string_view domain) {
    while (!domain.empty()) {
        if (is_tld(domain)) {
            return domain;
        }
        auto pos = domain.find('.');
        if (pos == domain.npos) {
            return std::nullopt;
        }
        domain = domain.substr(pos + 1);
    }
    return std::nullopt;
}

std::string_view tldregistry::reduce_domain(std::string_view domain, unsigned int n) {
    while (!domain.empty() && domain.front() == '.') {
        domain.remove_prefix(1);
    }
    while (!domain.empty() && domain.back() == '.') {
        domain.remove_suffix(1);
    }
    size_t pos;
    if (auto tld = get_tld(domain)) {
        pos = domain.size() - tld->size() - 2;
    } else if (pos = domain.rfind('.'); pos != domain.npos) {
        --pos;
    } else {
        return domain;
    }
    while (n > 0) {
        if ((pos = domain.rfind('.', pos)) == domain.npos) {
            return domain;
        }
        --pos;
        --n;
    }
    return domain.substr(pos + 2);
}
