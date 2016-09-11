#include <setup.h>
#include <appsup.h>
#include <netio.h>
#include "lib\mxml\mxml.h"

static PPH_STRING Version = NULL;
static PPH_STRING DownloadURL = NULL;

BOOLEAN QueryBuildServerThread(
    _Out_ PPH_STRING *Version,
    _Out_ PPH_STRING *DownloadURL
    )
{
    BOOLEAN isSuccess = FALSE;
    HINTERNET sessionHandle = NULL;
    HINTERNET connectionHandle = NULL;
    HINTERNET requestHandle = NULL;
    PSTR xmlStringBuffer = NULL;
    mxml_node_t* xmlNode = NULL;
    PPH_STRING versionString = NULL;
    PPH_STRING revisionString = NULL;
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig = { 0 };

    WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig);

    __try
    {
        STATUS_MSG(L"Connecting to build server...\n");

        if (!(sessionHandle = WinHttpOpen(
            NULL,
            proxyConfig.lpszProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            proxyConfig.lpszProxy ? proxyConfig.lpszProxy : WINHTTP_NO_PROXY_NAME,
            proxyConfig.lpszProxy ? proxyConfig.lpszProxyBypass : WINHTTP_NO_PROXY_BYPASS,
            0
            )))
        {
            STATUS_MSG(L"WinHttpOpen: %u\n", GetLastError());
            __leave;
        }

        if (!(connectionHandle = WinHttpConnect(
            sessionHandle,
            L"wj32.org",
            INTERNET_DEFAULT_HTTPS_PORT,
            0
            )))
        {
            STATUS_MSG(L"HttpConnect: %u\n", GetLastError());
            __leave;
        }

        STATUS_MSG(L"Connected to build server...\n");
        
        if (!(requestHandle = WinHttpOpenRequest(
            connectionHandle,
            NULL,
            L"/processhacker/update.php",
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
            )))
        {
            STATUS_MSG(L"HttpBeginRequest: %u\n", GetLastError());
            __leave;
        }

        if (!WinHttpAddRequestHeaders(
            requestHandle, 
            L"ProcessHacker-OsBuild: 0x0D06F00D", 
            -1L, 
            WINHTTP_ADDREQ_FLAG_ADD
            ))
        {
            STATUS_MSG(TEXT("HttpAddRequestHeaders: %u\n"), GetLastError());
            __leave;
        }

        if (!WinHttpSendRequest(
            requestHandle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
            ))
        {
            STATUS_MSG(L"HttpSendRequest: %u\n", GetLastError());
            __leave;
        }

        STATUS_MSG(L"Querying build server...");

        if (!WinHttpReceiveResponse(requestHandle, NULL))
        {
            STATUS_MSG(L"HttpEndRequest: %u\n", GetLastError());
            __leave;
        }

        if (!(xmlStringBuffer = HttpDownloadString(requestHandle)))
        {
            STATUS_MSG(L"HttpDownloadString: %u\n", GetLastError());
            __leave;
        }

        // Load our XML
        xmlNode = mxmlLoadString(NULL, xmlStringBuffer, MXML_OPAQUE_CALLBACK);
        if (xmlNode == NULL || xmlNode->type != MXML_ELEMENT)
            __leave;

        // Find the version node
        versionString = GetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "ver", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(versionString))
            __leave;

        // Find the revision node
        revisionString = GetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "rev", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(revisionString))
            __leave;

        *Version = PhFormatString(
            L"%s.%s",
            versionString->Buffer,
            revisionString->Buffer
            );

        *DownloadURL = PhFormatString(
            L"https://github.com/processhacker2/processhacker2/releases/download/v%s/processhacker-%s-bin.zip",
            versionString->Buffer,
            versionString->Buffer
            );

        STATUS_MSG(L"Found build: %s\n", *Version);

        isSuccess = TRUE;
    }
    __finally
    {
        if (requestHandle)
        {
            WinHttpCloseHandle(requestHandle);
        }

        if (connectionHandle)
        {
            WinHttpCloseHandle(connectionHandle);
        }

        if (sessionHandle)
        {
            WinHttpCloseHandle(sessionHandle);
        }

        if (revisionString)
        {
            PhDereferenceObject(revisionString);
        }

        if (versionString)
        {
            PhDereferenceObject(versionString);
        }

        if (xmlNode)
        {
            mxmlDelete(xmlNode);
        }

        if (xmlStringBuffer)
        {
            PhFree(xmlStringBuffer);
        }
    }

    return isSuccess;
}

PPH_STRING GenerateDownloadFilePath(
    _In_ PPH_STRING Version
    )
{
    BOOLEAN success = FALSE;
    ULONG indexOfFileName = -1;
    GUID randomGuid;
    PPH_STRING setupTempPath = NULL;
    PPH_STRING randomGuidString = NULL;
    PPH_STRING setupFilePath = NULL;
    PPH_STRING fullSetupPath = NULL;

    __try
    {
        // Allocate the GetTempPath buffer
        setupTempPath = PhCreateStringEx(NULL, GetTempPath(0, NULL) * sizeof(WCHAR));
        if (PhIsNullOrEmptyString(setupTempPath))
            __leave;

        // Get the temp path
        if (GetTempPath((ULONG)setupTempPath->Length / sizeof(WCHAR), setupTempPath->Buffer) == 0)
            __leave;
        if (PhIsNullOrEmptyString(setupTempPath))
            __leave;

        // Generate random guid for our directory path.
        PhGenerateGuid(&randomGuid);

        if (randomGuidString = PhFormatGuid(&randomGuid))
        {
            PPH_STRING guidSubString;

            // Strip the left and right curly brackets.
            guidSubString = PhSubstring(
                randomGuidString, 
                1, 
                randomGuidString->Length / sizeof(WCHAR) - 2
                );

            PhSwapReference(&randomGuidString, guidSubString);
        }

        // Append the tempath to our string: %TEMP%RandomString\\processhacker-%lu.%lu-setup.zip
        // Example: C:\\Users\\dmex\\AppData\\Temp\\ABCD\\processhacker-3.10-setup.zip
        setupFilePath = PhFormatString(
            L"%s%s\\processhacker-%s-setup.zip",
            setupTempPath->Buffer,
            randomGuidString->Buffer,
            Version->Buffer
            );

        // Create the directory if it does not exist.
        if (fullSetupPath = PhGetFullPath(setupFilePath->Buffer, &indexOfFileName))
        {
            PPH_STRING directoryPath;

            if (indexOfFileName == -1)
                __leave;

            if (directoryPath = PhSubstring(fullSetupPath, 0, indexOfFileName))
            {
                CreateDirectoryPath(directoryPath);

                PhDereferenceObject(directoryPath);
            }
        }

        success = TRUE;
    }
    __finally
    {
        if (!success)
        {
            if (setupFilePath)
            {
                PhDereferenceObject(setupFilePath);
            }
        }

        if (fullSetupPath)
        {
            PhDereferenceObject(fullSetupPath);
        }

        if (randomGuidString)
        {
            PhDereferenceObject(randomGuidString);
        }

        if (setupTempPath)
        {
            PhDereferenceObject(setupTempPath);
        }
    }

    return success ? setupFilePath : NULL;
}



BOOLEAN SetupDownloadBuild(
    _In_ PVOID Arguments
    )
{
    HINTERNET sessionHandle = NULL;
    HINTERNET connectionHandle = NULL;
    HINTERNET requestHandle = NULL;
    HANDLE tempFileHandle = NULL;
    ULONG contentLength = 0;
    BOOLEAN isDownloadSuccess = FALSE;
    HTTP_PARSED_URL urlComponents = NULL;
    ULONG writeLength = 0;
    ULONG readLength = 0;
    ULONG totalLength = 0;
    ULONG bytesDownloaded = 0;
    ULONG downloadedBytes = 0;
    ULONG contentLengthSize = sizeof(ULONG);
    PH_HASH_CONTEXT hashContext;
    IO_STATUS_BLOCK isb;
    BYTE buffer[PAGE_SIZE];
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig = { 0 };

    WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig);

    StartProgress();
    SendDlgItemMessage(Arguments, IDC_PROGRESS1, PBM_SETSTATE, PBST_NORMAL, 0);

    __try
    {
        if (!QueryBuildServerThread(&Version, &DownloadURL))
            __leave;




        GenerateDownloadFilePath(Version);



        if (!HttpParseURL(DownloadURL->Buffer, &urlComponents))
        {
            STATUS_MSG(L"HttpParseURL: %u\n", GetLastError());
            __leave;
        }

        STATUS_MSG(L"Connecting to download server...\n");

        if (!(sessionHandle = WinHttpOpen(
            NULL,
            proxyConfig.lpszProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            proxyConfig.lpszProxy ? proxyConfig.lpszProxy : WINHTTP_NO_PROXY_NAME,
            proxyConfig.lpszProxy ? proxyConfig.lpszProxyBypass : WINHTTP_NO_PROXY_BYPASS,
            0
            )))
        {
            STATUS_MSG(L"WinHttpOpen: %u\n", GetLastError());
            __leave;
        }

        if (!(connectionHandle = WinHttpConnect(
            sessionHandle,
            urlComponents->HttpServer,
            INTERNET_DEFAULT_HTTPS_PORT,
            0
            )))
        {
            STATUS_MSG(L"HttpConnect: %u\n", GetLastError());
            __leave;
        }

        STATUS_MSG(L"Connected to download server...\n");

        if (!(requestHandle = WinHttpOpenRequest(
            connectionHandle,
            NULL,
            urlComponents->HttpPath,
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
            )))
        {
            STATUS_MSG(L"HttpBeginRequest: %u\n", GetLastError());
            __leave;
        }

        if (!WinHttpAddRequestHeaders(
            requestHandle, 
            L"User-Agent: 0x0D06F00D", 
            -1L, 
            WINHTTP_ADDREQ_FLAG_ADD
            ))
        {
            STATUS_MSG(TEXT("HttpAddRequestHeaders: %u\n"), GetLastError());
            __leave;
        }

        if (!WinHttpSendRequest(
            requestHandle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
            ))
        {
            STATUS_MSG(L"HttpSendRequest: %u\n", GetLastError());
            __leave;
        }

        STATUS_MSG(L"Querying download server...");
        
        if (!WinHttpReceiveResponse(requestHandle, NULL))
        {
            STATUS_MSG(L"HttpEndRequest: %u\n", GetLastError());
            __leave;
        }

        if (!(contentLength = HttpGetRequestHeaderDword(
            requestHandle, 
            WINHTTP_QUERY_CONTENT_LENGTH
            )))
        {
            STATUS_MSG(L"HttpGetRequestHeaderDword: %u\n", GetLastError());
            __leave;
        }

        STATUS_MSG(L"Downloading latest build %s...\n", Version->Buffer);

        // Create output file
        if (!NT_SUCCESS(PhCreateFileWin32(
            &tempFileHandle,
            L"processhacker-2.38-bin.zip",
            FILE_GENERIC_READ | FILE_GENERIC_WRITE,
            FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_TEMPORARY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OVERWRITE_IF,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            )))
        {
            __leave;
        }

        // Initialize hash algorithm.
        PhInitializeHash(&hashContext, Sha1HashAlgorithm);

        // Zero the buffer.
        memset(buffer, 0, PAGE_SIZE);

        // Download the data.
        while (WinHttpReadData(requestHandle, buffer, PAGE_SIZE, &bytesDownloaded))
        {
            // If we get zero bytes, the file was uploaded or there was an error
            if (bytesDownloaded == 0)
                break;

            // Update the hash of bytes we downloaded.
            PhUpdateHash(&hashContext, buffer, bytesDownloaded);

            // Write the downloaded bytes to disk.
            if (!NT_SUCCESS(NtWriteFile(
                tempFileHandle,
                NULL,
                NULL,
                NULL,
                &isb,
                buffer,
                bytesDownloaded,
                NULL,
                NULL
                )))
            {
                __leave;
            }

            downloadedBytes += (DWORD)isb.Information;

            // Check the number of bytes written are the same we downloaded.
            if (bytesDownloaded != isb.Information)
                __leave;

            // Update the GUI progress.
            // TODO: Update on GUI thread.
            //{
            //    //int percent = MulDiv(100, downloadedBytes, contentLength);
            //    FLOAT percent = ((FLOAT)downloadedBytes / contentLength * 100);
            //    PPH_STRING totalDownloaded = PhFormatSize(downloadedBytes, -1);
            //    PPH_STRING totalLength = PhFormatSize(contentLength, -1);
            //
            //    PPH_STRING dlLengthString = PhFormatString(
            //        L"%s of %s (%.0f%%)",
            //        totalDownloaded->Buffer,
            //        totalLength->Buffer,
            //        percent
            //        );
            //
            //    // Update the progress bar position
            //    SendMessage(context->ProgressHandle, PBM_SETPOS, (ULONG)percent, 0);
            //    Static_SetText(context->StatusHandle, dlLengthString->Buffer);
            //
            //    PhDereferenceObject(dlLengthString);
            //    PhDereferenceObject(totalDownloaded);
            //    PhDereferenceObject(totalLength);
            //}

            SetProgress(downloadedBytes, contentLength);
        }

        // Compute hash result (will fail if file not downloaded correctly).
        //if (PhFinalHash(&hashContext, &hashBuffer, 20, NULL))
        //{
        //    // Allocate our hash string, hex the final hash result in our hashBuffer.
        //    PPH_STRING hexString = PhBufferToHexString(hashBuffer, 20);
        //
        //    if (PhEqualString(hexString, context->Hash, TRUE))
        //    {
        //        //hashSuccess = TRUE;
        //    }
        //
        //    PhDereferenceObject(hexString);
        //}
        //
        //    //if (_tcsstr(hashETag->Buffer, finalHexString->Buffer))
        //    {
        //        //DEBUG_MSG(TEXT("Hash success: %s (%s)\n"), SetupFileName->Buffer, finalHexString->Buffer);
        //    }
        //    //else
        //    //{
        //    //    SendDlgItemMessage(_hwndProgress, IDC_PROGRESS1, PBM_SETSTATE, PBST_ERROR, 0);
        //    //    SetDlgItemText(_hwndProgress, IDC_MAINHEADER, TEXT("Retrying download... Hash error."));
        //    //    DEBUG_MSG(TEXT("Hash error (retrying...): %s\n"), SetupFileName->Buffer);
        //    //}


        isDownloadSuccess = TRUE;
    }
    __finally
    {
        if (requestHandle)
        {
            WinHttpCloseHandle(requestHandle);
        }

        if (connectionHandle)
        {
            WinHttpCloseHandle(connectionHandle);
        }

        if (sessionHandle)
        {
            WinHttpCloseHandle(sessionHandle);
        }

        if (tempFileHandle)
        {
            NtClose(tempFileHandle);
        }

        if (urlComponents)
        {
            PhFree(urlComponents);
        }
    }

    return TRUE;
}