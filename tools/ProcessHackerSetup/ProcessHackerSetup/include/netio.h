#ifndef _PH_NETIO_H
#define _PH_NETIO_H

#pragma comment(lib, "Winhttp.lib")
#include <winhttp.h>

typedef struct _HTTP_PARSED_URL
{
    WCHAR HttpMethod[10];
    WCHAR HttpServer[200];
    WCHAR HttpPath[200];
} *HTTP_PARSED_URL;


PPH_STRING HttpGetRequestHeaderString(
    _In_ HINTERNET RequestHandle,
    _In_ PCWSTR RequestHeader
    );

ULONG HttpGetRequestHeaderDword(
    _In_ HINTERNET RequestHandle,
    _In_ ULONG Flags
    );

PSTR HttpDownloadString(
    _In_ HINTERNET RequestHandle
    );

BOOLEAN HttpParseURL(
    _In_ PCWSTR Url,
    _Out_ HTTP_PARSED_URL* HttpParsedUrl
    );

#endif