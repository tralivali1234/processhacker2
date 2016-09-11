#include <setup.h>
#include "netio.h"

//PVOID HttpGetRequestHeaderValue(
//    _Inout_ PSETUP_HTTP_SESSION HttpSocket,
//    _In_ LPCWSTR RequestHeader,
//    _In_ ULONG Flags
//    )
//{
//    PVOID buffer = NULL;
//    ULONG bufferSize = 0;
//    LRESULT keyResult = NO_ERROR;
//
//    // Get the length of the data...
//    if (!WinHttpQueryHeaders(
//        HttpSocket->RequestHandle,
//        Flags,
//        RequestHeader,
//        WINHTTP_NO_OUTPUT_BUFFER,
//        &bufferSize,
//        WINHTTP_NO_HEADER_INDEX
//        ))
//    {
//        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
//            return NULL;
//    }
//
//    // Allocate the buffer...
//    buffer = PhAllocate(bufferSize, NULL);
//
//    // Query the value...
//    if (!WinHttpQueryHeaders(
//        HttpSocket->RequestHandle,
//        Flags,
//        RequestHeader,
//        buffer,
//        &bufferSize,
//        WINHTTP_NO_HEADER_INDEX
//        ))
//    {
//        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
//            return NULL;
//    }
//
//    if (buffer)
//        return buffer;
//
//    PhFree(buffer);
//    return NULL;
//}

PPH_STRING HttpGetRequestHeaderString(
    _In_ HINTERNET RequestHandle,
    _In_ PCWSTR RequestHeader
    )
{
    ULONG bufferSize = 0;
    PPH_STRING stringBuffer = NULL;

    // Get the length of the data...
    if (!WinHttpQueryHeaders(
        RequestHandle,
        WINHTTP_QUERY_CUSTOM,
        RequestHeader,
        WINHTTP_NO_OUTPUT_BUFFER,
        &bufferSize,
        WINHTTP_NO_HEADER_INDEX
        ))
    {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return NULL;
    }

    // Allocate the buffer...
    stringBuffer = PhCreateStringEx(NULL, bufferSize);

    // Query the data value...
    if (WinHttpQueryHeaders(
        RequestHandle,
        WINHTTP_QUERY_CUSTOM,
        RequestHeader,
        stringBuffer->Buffer,
        &bufferSize,
        WINHTTP_NO_HEADER_INDEX
        ))
    {
        return stringBuffer;
    }

    PhDereferenceObject(stringBuffer);
    return NULL;
}

ULONG HttpGetRequestHeaderDword(
    _In_ HINTERNET RequestHandle,
    _In_ ULONG Flags
    )
{
    ULONG dwordResult = 0;
    ULONG dwordLength = sizeof(ULONG);
    ULONG dwordResultTemp = 0;

    if (WinHttpQueryHeaders(
        RequestHandle,
        Flags | WINHTTP_QUERY_FLAG_NUMBER,
        NULL,
        &dwordResultTemp,
        &dwordLength,
        WINHTTP_NO_HEADER_INDEX
        ))
    {
        dwordResult = dwordResultTemp;
    }

    return dwordResult;
}

PSTR HttpDownloadString(
    _In_ HINTERNET RequestHandle
    )
{
    PSTR tempDataPtr = NULL;
    PPH_STRING tempHttpString = NULL;
    ULONG dataLength = 0;
    ULONG returnLength = 0;
    ULONG allocatedLength = PAGE_SIZE;
    BYTE buffer[PAGE_SIZE];

    tempDataPtr = (PSTR)PhAllocate(allocatedLength);

    while (WinHttpReadData(RequestHandle, buffer, PAGE_SIZE, &returnLength))
    {
        if (returnLength == 0)
            break;

        if (allocatedLength < dataLength + returnLength)
        {
            allocatedLength *= 2;
            tempDataPtr = (PSTR)PhReAllocate(tempDataPtr, allocatedLength);
        }

        memcpy(tempDataPtr + dataLength, buffer, returnLength);
        memset(buffer, 0, returnLength);

        dataLength += returnLength;
    }

    // Add space for the null terminator..
    if (allocatedLength < dataLength + 1)
    {
        allocatedLength++;
        tempDataPtr = (PSTR)PhReAllocate(tempDataPtr, allocatedLength);
    }

    // Ensure that the buffer is null-terminated.
    tempDataPtr[dataLength] = 0;

    return tempDataPtr;
}

BOOLEAN HttpParseURL(
    _In_ PCWSTR Url,
    _Out_ HTTP_PARSED_URL* HttpParsedUrl
    )
{
    URL_COMPONENTS httpUrlComponents;

    memset(&httpUrlComponents, 0, sizeof(URL_COMPONENTS));

    httpUrlComponents.dwStructSize = sizeof(URL_COMPONENTS);
    httpUrlComponents.dwSchemeLength = (ULONG)-1;
    httpUrlComponents.dwHostNameLength = (ULONG)-1;
    httpUrlComponents.dwUrlPathLength = (ULONG)-1;

    if (WinHttpCrackUrl(
        Url,
        0,//(ULONG)wcslen(Url),
        0,
        &httpUrlComponents
        ))
    {
        HTTP_PARSED_URL httpParsedUrl = PhAllocate(sizeof(struct _HTTP_PARSED_URL));
        memset(httpParsedUrl, 0, sizeof(struct _HTTP_PARSED_URL));

        wmemcpy(httpParsedUrl->HttpMethod, httpUrlComponents.lpszScheme, httpUrlComponents.dwSchemeLength);
        wmemcpy(httpParsedUrl->HttpServer, httpUrlComponents.lpszHostName, httpUrlComponents.dwHostNameLength);
        wmemcpy(httpParsedUrl->HttpPath, httpUrlComponents.lpszUrlPath, httpUrlComponents.dwUrlPathLength);

        *HttpParsedUrl = httpParsedUrl;

        return TRUE;
    }

    return FALSE;
}