#include "address.h"
#include <windows.h>
#include <urlmon.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <oleauto.h>

namespace aqua {
namespace {
bool IsWebUrl(const std::wstring& value) {
    Microsoft::WRL::ComPtr<IUri> uri;
    if (FAILED(CreateUri(value.c_str(), Uri_CREATE_CANONICALIZE, 0, &uri)))
        return false;

    DWORD scheme = 0;
    if (FAILED(uri->GetScheme(&scheme)) ||
        (scheme != URL_SCHEME_HTTPS && scheme != URL_SCHEME_HTTP)) {
        return false;
    }

    BSTR host = nullptr;
    const auto result = uri->GetHost(&host);
    const bool valid = result == S_OK && SysStringLen(host) > 0;
    SysFreeString(host);
    return valid;
}
}

std::wstring ResolveAddress(std::wstring_view input) {
    const auto start = input.find_first_not_of(L" \t\r\n");
    if (start == std::wstring_view::npos) return {};
    input = input.substr(start, input.find_last_not_of(L" \t\r\n") - start + 1);
    const std::wstring value(input);
    if (value.find_first_of(L" \t\r\n") == std::wstring::npos) {
        if (IsWebUrl(value)) return value;
        const auto authority = input.substr(0, input.find_first_of(L"/?#"));
        const auto host = authority.substr(0, authority.find(L':'));
        if (value.find(L"://") == std::wstring::npos &&
            authority.find_first_of(L"@\\") == std::wstring_view::npos &&
            (host.find(L'.') != std::wstring_view::npos || host == L"localhost")) {
            const auto https = L"https://" + value;
            if (IsWebUrl(https)) return https;
        }
    }

    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        bytes.data(), count, nullptr, nullptr);
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring url = L"https://www.google.com/search?q=";
    for (const unsigned char byte : bytes) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
            byte == '.' || byte == '~') {
            url += static_cast<wchar_t>(byte);
        } else {
            url += L'%';
            url += hex[byte >> 4];
            url += hex[byte & 15];
        }
    }
    return url;
}
}
