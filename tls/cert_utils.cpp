#include <common/tls/cert_utils.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <cstdio>
#include <string>

namespace ag::tls {

static std::string asn1_time_to_string(const ASN1_TIME *t) {
    if (t == nullptr) {
        return "<null>";
    }
    BIO *bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return "<error>";
    }
    ASN1_TIME_print(bio, t);
    char buf[64] = {};
    int len = BIO_read(bio, buf, sizeof(buf) - 1);
    BIO_free(bio);
    if (len <= 0) {
        return "<error>";
    }
    return {buf, static_cast<size_t>(len)};
}

static void append_cert_info(std::string &out, X509 *cert, const char *label) {
    if (cert == nullptr) {
        return;
    }
    char subject_buf[256] = {};
    char issuer_buf[256] = {};
    X509_NAME_oneline(X509_get_subject_name(cert), subject_buf, sizeof(subject_buf));
    X509_NAME_oneline(X509_get_issuer_name(cert), issuer_buf, sizeof(issuer_buf));

    uint8_t sha256[32] = {};
    unsigned int sha256_len = 0;
    X509_digest(cert, EVP_sha256(), sha256, &sha256_len);

    char fp_buf[65] = {};
    for (unsigned int i = 0; i < sha256_len && i < 32; ++i) {
        snprintf(fp_buf + i * 2, 3, "%02x", sha256[i]);
    }

    std::string not_before = asn1_time_to_string(X509_get0_notBefore(cert));
    std::string not_after = asn1_time_to_string(X509_get0_notAfter(cert));

    char line[800];
    snprintf(line, sizeof(line), "%s: Subject: %s, Issuer: %s, SHA256: %s, NotBefore: %s, NotAfter: %s",
            label, subject_buf, issuer_buf, fp_buf, not_before.c_str(), not_after.c_str());
    out += line;
}

std::string get_cert_diagnostic_info(X509 *cert, STACK_OF(X509) *chain) {
    if (cert == nullptr) {
        return {};
    }
    std::string result;
    append_cert_info(result, cert, "Leaf");

    int chain_len = chain ? (int) sk_X509_num(chain) : 0;
    for (int i = 0; i < chain_len; ++i) {
        X509 *chain_cert = sk_X509_value(chain, i);
        if (chain_cert == cert) {
            continue;
        }
        result += '\n';
        char label[32];
        snprintf(label, sizeof(label), "Chain[%d]", i);
        append_cert_info(result, chain_cert, label);
    }
    return result;
}

} // namespace ag::tls
