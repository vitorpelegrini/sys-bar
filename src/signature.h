#pragma once

#include "common.h"

#include <optional>
#include <string>

// Authenticode checks for self-updates.
namespace signature {

// Subject (simple display name) of the certificate that signed `path`, when the file carries a valid
// signature chaining to a trusted root; nullopt for unsigned or invalid files.
std::optional<std::wstring> TrustedSigner(const std::wstring& path);

// Signer of this executable (nullopt while releases are not code-signed).
std::optional<std::wstring> OwnSigner();

}  // namespace signature
