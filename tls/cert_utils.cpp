#include <common/tls/cert_utils.h>

#include <openssl/x509.h>

#include <cstdio>

namespace ag::tls {

std::string get_cert_diagnostic_info(X509 *cert, STACK_OF(X509) *chain) {
    if (cert == nullptr) {
        return {};
    }
    char subject_buf[256] = {}, issuer_buf[256] = {};
    X509_NAME_oneline(X509_get_subject_name(cert), subject_buf, sizeof(subject_buf));
    X509_NAME_oneline(X509_get_issuer_name(cert), issuer_buf, sizeof(issuer_buf));
    char result[600];
    snprintf(result, sizeof(result), "Subject: %s, Issuer: %s, Chain length: %d",
            subject_buf, issuer_buf, chain ? sk_X509_num(chain) : 0);
    return result;
}

} // namespace ag::tls
