#include "signature.h"

#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

namespace signature {

std::optional<std::wstring> TrustedSigner(const std::wstring& path) {
    WINTRUST_FILE_INFO file{sizeof(file)};
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{sizeof(data)};
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;  // offline-friendly; the chain itself is still validated
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    std::optional<std::wstring> subject;
    if (WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data) == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* cert = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
        if (cert && cert->pCert) {
            wchar_t name[256] = {};
            if (CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name,
                                   static_cast<DWORD>(std::size(name))) > 1) {
                subject = name;
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return subject;
}

std::optional<std::wstring> OwnSigner() {
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (n == 0 || n >= std::size(path)) return std::nullopt;
    return TrustedSigner(path);
}

}  // namespace signature
