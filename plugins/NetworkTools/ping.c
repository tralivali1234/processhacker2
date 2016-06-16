/*
 * Process Hacker Network Tools -
 *   Ping dialog
 *
 * Copyright (C) 2015 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "nettools.h"
#include <commonutil.h>
#include <winhttp.h>
#include <math.h>

PSTR CdnUserLat;
PSTR CdnUserLong;
#define WM_PING_UPDATE (WM_APP + 151)

static RECT NormalGraphTextMargin = { 5, 5, 5, 5 };
static RECT NormalGraphTextPadding = { 3, 3, 3, 3 };

BOOLEAN ReadRequestString(
    _In_ HINTERNET Handle,
    _Out_ _Deref_post_z_cap_(*DataLength) PSTR *Data,
    _Out_ ULONG *DataLength)
{
    PSTR data;
    ULONG allocatedLength;
    ULONG dataLength;
    ULONG returnLength;
    BYTE buffer[PAGE_SIZE];

    allocatedLength = sizeof(buffer);
    data = (PSTR)PhAllocate(allocatedLength);
    dataLength = 0;

    // Zero the buffer
    memset(buffer, 0, PAGE_SIZE);

    while (WinHttpReadData(Handle, buffer, PAGE_SIZE, &returnLength))
    {
        if (returnLength == 0)
            break;

        if (allocatedLength < dataLength + returnLength)
        {
            allocatedLength *= 2;
            data = (PSTR)PhReAllocate(data, allocatedLength);
        }

        // Copy the returned buffer into our pointer
        memcpy(data + dataLength, buffer, returnLength);
        // Zero the returned buffer for the next loop
        //memset(buffer, 0, returnLength);

        dataLength += returnLength;
    }

    if (allocatedLength < dataLength + 1)
    {
        allocatedLength++;
        data = (PSTR)PhReAllocate(data, allocatedLength);
    }

    // Ensure that the buffer is null-terminated.
    data[dataLength] = 0;

    *DataLength = dataLength;
    *Data = data;

    return TRUE;
}

static PSTR XmlParseGeoToken(
    _In_ PSTR XmlString,
    _In_ PSTR XmlTokenName,
    _In_ PSTR XmlAttrName)
{
    PSTR xmlTokenNext = NULL;
    PSTR xmlStringData = _strdup(XmlString);
    PSTR xmlTokenString = strtok_s(
        xmlStringData,
        "< >",
        &xmlTokenNext);

    __try
    {
        while (xmlTokenString)
        {
            if (_stricmp(xmlTokenString, XmlTokenName) == 0)
            {
                while (xmlTokenString)
                {
                    if (_stricmp(xmlTokenString, XmlAttrName) == 0)
                    {
                        xmlTokenString = strtok_s(NULL, "\n<>", &xmlTokenNext);
                        __leave;
                    }

                    xmlTokenString = strtok_s(NULL, "\n<>", &xmlTokenNext);
                }
            }

            xmlTokenString = strtok_s(NULL, "<>", &xmlTokenNext);
        }
    }
    __finally
    {

    }

    if (xmlTokenString)
    {
        PSTR xmlStringDup = _strdup(xmlTokenString);
        free(xmlStringData);
        return xmlStringDup;
    }

    free(xmlStringData);
    return NULL;
}

static PSTR XmlParseAttrToken(
    __in PSTR XmlString,
    __in PSTR XmlTokenName,
    __in PSTR XmlAttrName)
{
    PSTR xmlTokenNext = NULL;
    PSTR xmlStringData = _strdup(XmlString);
    PSTR xmlTokenString = strtok_s(
        xmlStringData,
        "< >",
        &xmlTokenNext);

    while (xmlTokenString)
    {
        if (!_stricmp(xmlTokenString, XmlTokenName))
        {
            // We found the value.
            while (xmlTokenString)
            {
                if (!_stricmp(xmlTokenString, XmlAttrName))
                {
                    // We found the Attribute.
                    xmlTokenString = strtok_s(NULL, "\"", &xmlTokenNext);
                    break;
                }

                xmlTokenString = strtok_s(NULL, "= \"", &xmlTokenNext);
            }

            if (xmlTokenString)
                break;
        }

        xmlTokenString = strtok_s(NULL, "< >", &xmlTokenNext);
    }

    if (xmlTokenString)
    {
        PSTR xmlStringDup = _strdup(xmlTokenString);
        free(xmlStringData);
        return xmlStringDup;
    }

    free(xmlStringData);
    return NULL;
}

VOID NetworkPingUpdateGraph(
    _In_ PNETWORK_OUTPUT_CONTEXT Context)
{
    Context->PingGraphState.Valid = FALSE;
    Context->PingGraphState.TooltipIndex = -1;
    Graph_MoveGrid(Context->PingGraphHandle, 1);
    Graph_Draw(Context->PingGraphHandle);
    Graph_UpdateTooltip(Context->PingGraphHandle);
    InvalidateRect(Context->PingGraphHandle, NULL, FALSE);
}




/*  Definitions:                                                           */
/*    South latitudes are negative, east longitudes are positive           */
/*                                                                         */
/*  Passed to function:                                                    */
/*    lat1, lon1 = Latitude and Longitude of point 1 (in decimal degrees)  */
/*    lat2, lon2 = Latitude and Longitude of point 2 (in decimal degrees)  */
/*    unit = the unit you desire for results                               */
/*           where: 'M' is statute miles                                   */
/*                  'K' is kilometers (default)                            */
/*                  'N' is nautical miles   (distance = distance * 0.8684) */
#define pi 3.14159265358979323846

/* decimal degrees to radians */
static __inline double deg2rad(double deg)
{
    return (deg * pi / 180);
}

/* radians to decimal degrees */
static __inline double rad2deg(double rad)
{
    return (rad * 180 / pi);
}


DOUBLE GeoDistance(
    __in DOUBLE Latitude,
    __in DOUBLE Longitude,
    __in DOUBLE CompareLatitude,
    __in DOUBLE CompareLongitude)
{
    DOUBLE theta = 0;
    DOUBLE distance = 0;

    theta = Longitude - CompareLongitude;
    distance = sin(deg2rad(Latitude)) * sin(deg2rad(CompareLatitude)) + cos(deg2rad(Latitude)) * cos(deg2rad(CompareLatitude)) * cos(deg2rad(theta));
    distance = acos(distance);
    distance = rad2deg(distance);
    distance = distance * 60 * 1.1515;

    //if (!LocaleIsMetric())
    {
        distance = distance * 1.609344;

       // WriteStringFormatFileStream(_T("Minimum distance estimation: "));
        //StringFormatColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY, _T("%.3fkm\n"), distance);
    }
    //else
    {
       // WriteStringFormatFileStream(_T("Minimum distance estimation: "));
        //StringFormatColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY, _T("%.3fmi\n"), distance);
    }

    return distance;
}




VOID LookupSpeedTestConfig(
    _In_ PNETWORK_OUTPUT_CONTEXT Context,
    _In_ IPAddr ipv4_Addr)
{
    HINTERNET httpSessionHandle = NULL;
    HINTERNET httpConnectionHandle = NULL;
    HINTERNET httpRequestHandle = NULL;
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig = { 0 };

    WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig);

    __try
    {
        if (!(httpSessionHandle = WinHttpOpen(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.84 Safari/537.36",
            proxyConfig.lpszProxy != NULL ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            proxyConfig.lpszProxy,
            proxyConfig.lpszProxyBypass,
            0)))
        {
            __leave;
        }

        if (WindowsVersion >= WINDOWS_8_1)
        {
            // Enable GZIP and DEFLATE support on Windows 8.1 and above using undocumented flags.
            ULONG httpFlags = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;

            WinHttpSetOption(
                httpSessionHandle,
                WINHTTP_OPTION_DECOMPRESSION,
                &httpFlags,
                sizeof(ULONG));
        }

        if (!(httpConnectionHandle = WinHttpConnect(
            httpSessionHandle,
            L"www.speedtest.net",
            INTERNET_DEFAULT_HTTP_PORT,
            0)))
        {
            __leave;
        }

        if (!(httpRequestHandle = WinHttpOpenRequest(
            httpConnectionHandle,
            NULL,
            L"/speedtest-config.php",
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_REFRESH)))
        {
             __leave;
        }

        WinHttpAddRequestHeaders(
            httpRequestHandle,
            L"Accept: text/html,application/xhtml+xml,application/xml",
            -1,
            WINHTTP_ADDREQ_FLAG_ADD);

        //if (WindowsVersion >= WINDOWS_7)
        //{
        //    ULONG keepAlive = WINHTTP_DISABLE_KEEP_ALIVE;
        //    WinHttpSetOption(httpRequestHandle, WINHTTP_OPTION_DISABLE_FEATURE, &keepAlive, sizeof(ULONG));
        //}

        if (!WinHttpSendRequest(
            httpRequestHandle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH,
            0))
        {
            __leave;
        }

        if (WinHttpReceiveResponse(httpRequestHandle, NULL))
        {
            ULONG xmlStringBufferLength = 0;
            PSTR xmlStringBuffer = NULL;

            if (!ReadRequestString(httpRequestHandle, &xmlStringBuffer, &xmlStringBufferLength))
            {
                __leave;
            }

            // Check the buffer for valid data.
            if (xmlStringBuffer == NULL || xmlStringBuffer[0] == '\0')
            {
                __leave;
            }

            // dlAvgString = StringFormatSize(_atoi64(this->CdnUserDlAverageISP) * 1000);
            // ulAvgString = StringFormatSize(_atoi64(this->CdnUserUlAverageISP) * 1000);

            PSTR CdnUserIpAddress = XmlParseAttrToken(xmlStringBuffer, "client", "ip");
            PSTR CdnUserISP = XmlParseAttrToken(xmlStringBuffer, "client", "isp");
            PSTR CdnUserDlAverageISP = XmlParseAttrToken(xmlStringBuffer, "client", "ispdlavg");
            PSTR CdnUserUlAverageISP = XmlParseAttrToken(xmlStringBuffer, "client", "ispulavg");

            CdnUserLat = XmlParseAttrToken(xmlStringBuffer, "client", "lat");
            CdnUserLong = XmlParseAttrToken(xmlStringBuffer, "client", "lon");
        }
    }
    __finally
    {
        if (httpRequestHandle)
        {
            WinHttpCloseHandle(httpRequestHandle);
        }

        WinHttpCloseHandle(httpConnectionHandle);
        WinHttpCloseHandle(httpSessionHandle);
    }
}

VOID QueryServerGeoLocation(
    _In_ PNETWORK_OUTPUT_CONTEXT Context,
    IPAddr ipv4_Addr)
{
    HINTERNET httpSessionHandle = NULL;
    HINTERNET httpConnectionHandle = NULL;
    HINTERNET httpRequestHandle = NULL;
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig = { 0 };

    // Query the current system proxy
    WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig);

    __try
    {
        // Open the HTTP session with the system proxy configuration if available
        if (!(httpSessionHandle = WinHttpOpen(
            L"",
            proxyConfig.lpszProxy != NULL ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            proxyConfig.lpszProxy,
            proxyConfig.lpszProxyBypass,
            0)))
        {
            __leave;
        }

        if (WindowsVersion >= WINDOWS_8_1)
        {
            // Enable GZIP and DEFLATE support on Windows 8.1 and above using undocumented flags.
            ULONG httpFlags = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;

            WinHttpSetOption(
                httpSessionHandle,
                WINHTTP_OPTION_DECOMPRESSION,
                &httpFlags,
                sizeof(ULONG));
        }

        if (!(httpConnectionHandle = WinHttpConnect(
            httpSessionHandle,
            L"geoip.prototypeapp.com",
            INTERNET_DEFAULT_HTTP_PORT,
            0)))
        {
            __leave;
        }

        PPH_STRING ipAddrString = PhFormatString(L"/api/locate?format=xml&ip=%d.%d.%d.%d",
            (ipv4_Addr >> 0) & 255,
            (ipv4_Addr >> 8) & 255,
            (ipv4_Addr >> 16) & 255,
            (ipv4_Addr >> 24) & 255);

        if (!(httpRequestHandle = WinHttpOpenRequest(
            httpConnectionHandle,
            NULL,
            ipAddrString->Buffer,
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_REFRESH)))
        {
             __leave;
        }

        //if (WindowsVersion >= WINDOWS_7)
        //{
        //    ULONG keepAlive = WINHTTP_DISABLE_KEEP_ALIVE;
        //    WinHttpSetOption(httpRequestHandle, WINHTTP_OPTION_DISABLE_FEATURE, &keepAlive, sizeof(ULONG));
        //}

        if (!WinHttpSendRequest(
            httpRequestHandle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH,
            0))
        {
            __leave;
        }

        if (WinHttpReceiveResponse(httpRequestHandle, NULL))
        {
            ULONG xmlStringBufferLength = 0;
            PSTR xmlStringBuffer = NULL;
            PSTR serverLatitude = NULL;
            PSTR serverLongitude = NULL;

            if (!ReadRequestString(httpRequestHandle, &xmlStringBuffer, &xmlStringBufferLength))
            {
                __leave;
            }

            // Check the buffer for valid data.
            if (xmlStringBuffer == NULL || xmlStringBuffer[0] == '\0')
            {
                __leave;
            }

            if (!(serverLatitude = XmlParseGeoToken(xmlStringBuffer, "coords", "latitude")))
            {
                __leave;
            }

            if (!(serverLongitude = XmlParseGeoToken(xmlStringBuffer, "coords", "longitude")))
            {
                  __leave;
            }

            // Print the server distance.
            double dist = GeoDistance(atof(CdnUserLat), atof(CdnUserLong), atof(serverLatitude), atof(serverLongitude));

            SetDlgItemText(Context->WindowHandle, IDC_DISTANCE, PhFormatString(L"Distance (Est): %.3fkm", dist)->Buffer);
        }
    }
    __finally
    {
        if (httpRequestHandle)
        {
            WinHttpCloseHandle(httpRequestHandle);
        }

        WinHttpCloseHandle(httpConnectionHandle);
        WinHttpCloseHandle(httpSessionHandle);
    }
}

NTSTATUS NetworkPingThreadStart(
    _In_ PVOID Parameter
    )
{
    HANDLE icmpHandle = INVALID_HANDLE_VALUE;
    ULONG icmpCurrentPingMs = 0;
    ULONG icmpReplyCount = 0;
    ULONG icmpReplyLength = 0;
    PVOID icmpReplyBuffer = NULL;
    PPH_BYTES icmpEchoBuffer = NULL;
    PPH_STRING icmpRandString = NULL;
    IP_OPTION_INFORMATION pingOptions =
    {
        255,         // Time To Live
        0,           // Type Of Service
        IP_FLAG_DF,  // IP header flags
        0            // Size of options data
    };
    //pingOptions.Flags |= IP_FLAG_REVERSE;

    ULONG pingFirstMs = 0;
    ULONG pingSecondMs = 0;
    ULONG pingTotalMs = 0;
    ULONG pingCount = 0;
    ULONG pingSpeed = 0;
    PNETWORK_OUTPUT_CONTEXT context = (PNETWORK_OUTPUT_CONTEXT)Parameter;

    if (icmpRandString = PhCreateStringEx(NULL, PhGetIntegerSetting(SETTING_NAME_PING_SIZE) * 2 + 2))
    {
        PhGenerateRandomAlphaString(icmpRandString->Buffer, (ULONG)icmpRandString->Length / sizeof(WCHAR));

        icmpEchoBuffer = PhConvertUtf16ToMultiByte(icmpRandString->Buffer);
        PhDereferenceObject(icmpRandString);
    }

    __try
    {
        if (context->RemoteEndpoint.Address.Type == PH_IPV6_NETWORK_TYPE)
        {
            SOCKADDR_IN6 icmp6LocalAddr = { 0 };
            SOCKADDR_IN6 icmp6RemoteAddr = { 0 };
            PICMPV6_ECHO_REPLY2 icmp6ReplyStruct = NULL;

            // Create ICMPv6 handle.
            if ((icmpHandle = Icmp6CreateFile()) == INVALID_HANDLE_VALUE)
                __leave;

            // Set Local IPv6-ANY address.
            icmp6LocalAddr.sin6_addr = in6addr_any;
            icmp6LocalAddr.sin6_family = AF_INET6;

            // Set Remote IPv6 address.
            icmp6RemoteAddr.sin6_addr = context->RemoteEndpoint.Address.In6Addr;
            //icmp6RemoteAddr.sin6_port = _byteswap_ushort((USHORT)context->NetworkItem->RemoteEndpoint.Port);

            // Allocate ICMPv6 message.
            icmpReplyLength = ICMP_BUFFER_SIZE(sizeof(ICMPV6_ECHO_REPLY), icmpEchoBuffer);
            icmpReplyBuffer = PhAllocate(icmpReplyLength);
            memset(icmpReplyBuffer, 0, icmpReplyLength);

            InterlockedIncrement(&context->PingSentCount);

            // Send ICMPv6 ping...
            icmpReplyCount = Icmp6SendEcho2(
                icmpHandle,
                NULL,
                NULL,
                NULL,
                &icmp6LocalAddr,
                &icmp6RemoteAddr,
                icmpEchoBuffer->Buffer,
                (USHORT)icmpEchoBuffer->Length,
                &pingOptions,
                icmpReplyBuffer,
                icmpReplyLength,
                context->MaxPingTimeout
                );

            icmp6ReplyStruct = (PICMPV6_ECHO_REPLY2)icmpReplyBuffer;

            if (icmpReplyCount > 0 && icmp6ReplyStruct)
            {
                BOOLEAN icmpPacketSignature = FALSE;

                if (icmp6ReplyStruct->Status != IP_SUCCESS)
                {
                    InterlockedIncrement(&context->PingLossCount);
                }

                if (_memicmp(
                    icmp6ReplyStruct->Address.sin6_addr,
                    context->RemoteEndpoint.Address.In6Addr.u.Word,
                    sizeof(icmp6ReplyStruct->Address.sin6_addr)
                    ) != 0)
                {
                    InterlockedIncrement(&context->UnknownAddrCount);
                }

                icmpPacketSignature = _memicmp(
                    icmpEchoBuffer->Buffer,
                    icmp6ReplyStruct->Data,
                    icmpEchoBuffer->Length
                    ) == 0;

                if (!icmpPacketSignature)
                {
                    InterlockedIncrement(&context->HashFailCount);
                }

                icmpCurrentPingMs = icmp6ReplyStruct->RoundTripTime;
            }
            else
            {
                InterlockedIncrement(&context->PingLossCount);
            }
        }
        else
        {
            IPAddr icmpLocalAddr = 0;
            IPAddr icmpRemoteAddr = 0;
            BOOLEAN icmpPacketSignature = FALSE;
            PICMP_ECHO_REPLY icmpReplyStruct = NULL;

            // Create ICMPv4 handle.
            if ((icmpHandle = IcmpCreateFile()) == INVALID_HANDLE_VALUE)
                __leave;

            // Set Local IPv4-ANY address.
            icmpLocalAddr = in4addr_any.s_addr;

            // Set Remote IPv4 address.
            icmpRemoteAddr = context->RemoteEndpoint.Address.InAddr.s_addr;

            // Allocate ICMPv4 message.
            icmpReplyLength = ICMP_BUFFER_SIZE(sizeof(ICMP_ECHO_REPLY), icmpEchoBuffer);
            icmpReplyBuffer = PhAllocate(icmpReplyLength);
            memset(icmpReplyBuffer, 0, icmpReplyLength);

            InterlockedIncrement(&context->PingSentCount);

            // First ping with no data...
            // Send ICMPv4 ping...
            icmpReplyCount = IcmpSendEcho2Ex(
                icmpHandle,
                NULL,
                NULL,
                NULL,
                icmpLocalAddr,
                icmpRemoteAddr,
                NULL,
                0,
                &pingOptions,
                icmpReplyBuffer,
                icmpReplyLength,
                context->MaxPingTimeout);

            icmpReplyStruct = (PICMP_ECHO_REPLY)icmpReplyBuffer;

            if (icmpReplyStruct->Status != IP_SUCCESS)
            {
                InterlockedIncrement(&context->PingLossCount);
            }

            if (icmpReplyStruct->Address != context->RemoteEndpoint.Address.InAddr.s_addr)
            {
                InterlockedIncrement(&context->UnknownAddrCount);
            }

            if (icmpReplyStruct->DataSize == icmpEchoBuffer->Length)
            {
                icmpPacketSignature = RtlEqualMemory(
                    icmpEchoBuffer->Buffer,
                    icmpReplyStruct->Data,
                    icmpReplyStruct->DataSize);
            }

            if (!icmpPacketSignature)
            {
                InterlockedIncrement(&context->HashFailCount);
            }

            if (icmpReplyStruct->Status == IP_SUCCESS)
            {
                icmpCurrentPingMs = icmpReplyStruct->RoundTripTime;
                pingFirstMs = icmpReplyStruct->RoundTripTime;

                LookupSpeedTestConfig(context, icmpReplyStruct->Address);
                QueryServerGeoLocation(context, icmpReplyStruct->Address);
            }

            // Second ping with dwReplySize data...
            icmpReplyCount = IcmpSendEcho2Ex(
                icmpHandle,
                NULL,
                NULL,
                NULL,
                icmpLocalAddr,
                icmpRemoteAddr,
                icmpEchoBuffer->Buffer,
                (USHORT)icmpEchoBuffer->Length,
                &pingOptions,
                icmpReplyBuffer,
                icmpReplyLength,
                context->MaxPingTimeout);

            icmpReplyStruct = (PICMP_ECHO_REPLY)icmpReplyBuffer;

            if (icmpReplyLength == 0)
            {
                //WriteStringFormatFileStream(_T("Failed: %d\n"), GetLastError());
            }

            if (icmpReplyStruct->Status == IP_SUCCESS)
            {
                pingSecondMs = icmpReplyStruct->RoundTripTime;
            }

            if (pingFirstMs > pingSecondMs)
            {
                //break;
            }
            else if (pingFirstMs == pingSecondMs)
            {
                //break;
            }
            else
            {
                pingTotalMs += (pingSecondMs - pingFirstMs);
                pingCount++;

                if (pingTotalMs > 0)
                {
                    pingTotalMs = (pingTotalMs / pingCount);
                    pingSpeed = ((((icmpReplyLength * 2) * 1000) / pingTotalMs) * BITS_IN_ONE_BYTE); // 8 Kb (kilobits) in every KB (kilobyte)
                                                                                                 // [%d Kbps] = (pingSpeed / 1024)
                    if (pingSpeed)
                    {
                        PPH_STRING speed = PhFormatSize(pingSpeed, -1);

                        SetDlgItemText(context->WindowHandle, IDC_SPEEDTEXT,
                            PhaFormatString(L"Speed (Est): %s/s", speed->Buffer)->Buffer);

                        PhDereferenceObject(speed);
                    }
                }
            }
        }

        InterlockedIncrement(&context->PingRecvCount);

        if (context->PingMinMs == 0 || icmpCurrentPingMs < context->PingMinMs)
            context->PingMinMs = icmpCurrentPingMs;
        if (icmpCurrentPingMs > context->PingMaxMs)
            context->PingMaxMs = icmpCurrentPingMs;

        context->CurrentPingMs = icmpCurrentPingMs;

        PhAddItemCircularBuffer_ULONG(&context->PingHistory, icmpCurrentPingMs);
    }
    __finally
    {
        if (icmpEchoBuffer)
        {
            PhDereferenceObject(icmpEchoBuffer);
        }

        if (icmpHandle != INVALID_HANDLE_VALUE)
        {
            IcmpCloseHandle(icmpHandle);
        }

        if (icmpReplyBuffer)
        {
            PhFree(icmpReplyBuffer);
        }
    }

    PostMessage(context->WindowHandle, WM_PING_UPDATE, 0, 0);

    return STATUS_SUCCESS;
}

VOID NTAPI NetworkPingUpdateHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PNETWORK_OUTPUT_CONTEXT context = (PNETWORK_OUTPUT_CONTEXT)Context;

    // Queue up the next ping into our work queue...
    PhQueueItemWorkQueue(
        &context->PingWorkQueue,
        NetworkPingThreadStart,
        (PVOID)context
        );
}

INT_PTR CALLBACK NetworkPingWndProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PNETWORK_OUTPUT_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PNETWORK_OUTPUT_CONTEXT)lParam;
        SetProp(hwndDlg, L"Context", (HANDLE)context);
    }
    else
    {
        context = (PNETWORK_OUTPUT_CONTEXT)GetProp(hwndDlg, L"Context");
    }

    if (context == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PH_RECTANGLE windowRectangle;
            PPH_LAYOUT_ITEM panelItem;

            // We have already set the group boxes to have WS_EX_TRANSPARENT to fix
            // the drawing issue that arises when using WS_CLIPCHILDREN. However
            // in removing the flicker from the graphs the group boxes will now flicker.
            // It's a good tradeoff since no one stares at the group boxes.
            PhSetWindowStyle(hwndDlg, WS_CLIPCHILDREN, WS_CLIPCHILDREN);

            context->WindowHandle = hwndDlg;
            context->StatusHandle = GetDlgItem(hwndDlg, IDC_MAINTEXT);
            context->MaxPingTimeout = PhGetIntegerSetting(SETTING_NAME_PING_MINIMUM_SCALING);

            windowRectangle.Position = PhGetIntegerPairSetting(SETTING_NAME_PING_WINDOW_POSITION);
            windowRectangle.Size = PhGetScalableIntegerPairSetting(SETTING_NAME_PING_WINDOW_SIZE, TRUE).Pair;

            // Create the font handle.
            context->FontHandle = CommonCreateFont(-15, context->StatusHandle);

            // Create the graph control.
            context->PingGraphHandle = CreateWindow(
                PH_GRAPH_CLASSNAME,
                NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER,
                0,
                0,
                3,
                3,
                hwndDlg,
                NULL,
                NULL,
                NULL
                );
            Graph_SetTooltip(context->PingGraphHandle, TRUE);

            // Load the Process Hacker icon.
            context->IconHandle = (HICON)LoadImage(
                NtCurrentPeb()->ImageBaseAddress,
                MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER),
                IMAGE_ICON,
                GetSystemMetrics(SM_CXICON),
                GetSystemMetrics(SM_CYICON),
                LR_SHARED
                );
            // Set window icon.
            if (context->IconHandle)
                SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)context->IconHandle);

            // Initialize the WorkQueue with a maximum of 20 threads (fix pinging slow-links with a high interval update).
            PhInitializeWorkQueue(&context->PingWorkQueue, 0, 20, 5000);
            PhInitializeGraphState(&context->PingGraphState);
            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhInitializeCircularBuffer_ULONG(&context->PingHistory, PhGetIntegerSetting(L"SampleCount"));

            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_ICMP_PANEL), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_ICMP_AVG), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_ICMP_MIN), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_ICMP_MAX), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_PINGS_SENT), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_PINGS_LOST), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_BAD_HASH), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_ANON_ADDR), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_SPEEDTEXT), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_DISTEXT), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
           

            panelItem = PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_PING_LAYOUT), NULL, PH_ANCHOR_ALL);
            PhAddLayoutItemEx(&context->LayoutManager, context->PingGraphHandle, NULL, PH_ANCHOR_ALL, panelItem->Margin);

            // Load window settings.
            if (windowRectangle.Position.X == 0 || windowRectangle.Position.Y == 0)
                PhCenterWindow(hwndDlg, GetParent(hwndDlg));
            else
            {
                PhLoadWindowPlacementFromSetting(SETTING_NAME_PING_WINDOW_POSITION, SETTING_NAME_PING_WINDOW_SIZE, hwndDlg);
            }

            // Initialize window layout.
            PhLayoutManagerLayout(&context->LayoutManager);

            // Convert IP Address to string format.
            if (context->RemoteEndpoint.Address.Type == PH_IPV4_NETWORK_TYPE)
            {
                RtlIpv4AddressToString(&context->RemoteEndpoint.Address.InAddr, context->IpAddressString);
            }
            else
            {
                RtlIpv6AddressToString(&context->RemoteEndpoint.Address.In6Addr, context->IpAddressString);
            }

            SetWindowText(hwndDlg, PhaFormatString(L"Ping %s", context->IpAddressString)->Buffer);
            SetWindowText(context->StatusHandle, PhaFormatString(L"Pinging %s with %lu bytes of data...", context->IpAddressString, PhGetIntegerSetting(SETTING_NAME_PING_SIZE))->Buffer);

            PhRegisterCallback(
                &PhProcessesUpdatedEvent,
                NetworkPingUpdateHandler,
                context,
                &context->ProcessesUpdatedRegistration
                );
        }
        return TRUE;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
            case IDOK:
                DestroyWindow(hwndDlg);
                break;
            }
        }
        break;
    case WM_DESTROY:
        {
            PhUnregisterCallback(
                &PhProcessesUpdatedEvent,
                &context->ProcessesUpdatedRegistration
                );

            PhSaveWindowPlacementToSetting(
                SETTING_NAME_PING_WINDOW_POSITION,
                SETTING_NAME_PING_WINDOW_SIZE,
                hwndDlg
                );

            if (context->PingGraphHandle)
                DestroyWindow(context->PingGraphHandle);

            if (context->IconHandle)
                DestroyIcon(context->IconHandle);

            if (context->FontHandle)
                DeleteObject(context->FontHandle);

            PhDeleteWorkQueue(&context->PingWorkQueue);
            PhDeleteGraphState(&context->PingGraphState);
            PhDeleteLayoutManager(&context->LayoutManager);

            RemoveProp(hwndDlg, L"Context");
            PhFree(context);

            PostQuitMessage(0);
        }
        break;
    case WM_SIZE:
        PhLayoutManagerLayout(&context->LayoutManager);
        break;
    case WM_SIZING:
        PhResizingMinimumSize((PRECT)lParam, wParam, 420, 250);
        break;
    case WM_PING_UPDATE:
        {
            ULONG maxGraphHeight = 0;
            ULONG pingAvgValue = 0;

            NetworkPingUpdateGraph(context);

            for (ULONG i = 0; i < context->PingHistory.Count; i++)
            {
                maxGraphHeight = maxGraphHeight + PhGetItemCircularBuffer_ULONG(&context->PingHistory, i);
                pingAvgValue = maxGraphHeight / context->PingHistory.Count;
            }

            SetDlgItemText(hwndDlg, IDC_ICMP_AVG, PhaFormatString(
                L"Average: %lums", pingAvgValue)->Buffer);
            SetDlgItemText(hwndDlg, IDC_ICMP_MIN, PhaFormatString(
                L"Minimum: %lums", context->PingMinMs)->Buffer);
            SetDlgItemText(hwndDlg, IDC_ICMP_MAX, PhaFormatString(
                L"Maximum: %lums", context->PingMaxMs)->Buffer);

            SetDlgItemText(hwndDlg, IDC_PINGS_SENT, PhaFormatString(
                L"Pings sent: %lu", context->PingSentCount)->Buffer);
            SetDlgItemText(hwndDlg, IDC_PINGS_LOST, PhaFormatString(
                L"Pings lost: %lu (%.0f%%)", context->PingLossCount,
                ((FLOAT)context->PingLossCount / context->PingSentCount * 100))->Buffer);

            //SetDlgItemText(hwndDlg, IDC_BAD_HASH, PhaFormatString(
            //    L"Bad hashes: %lu", context->HashFailCount)->Buffer);
            SetDlgItemText(hwndDlg, IDC_ANON_ADDR, PhaFormatString(
                L"Anon replies: %lu", context->UnknownAddrCount)->Buffer);
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case GCN_GETDRAWINFO:
                {
                    PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)header;
                    PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

                    if (header->hwndFrom == context->PingGraphHandle)
                    {
                        if (PhGetIntegerSetting(L"GraphShowText"))
                        {
                            HDC hdc = Graph_GetBufferedContext(context->PingGraphHandle);

                            PhMoveReference(&context->PingGraphState.Text,
                                PhFormatString(L"Ping: %lums", context->CurrentPingMs)
                                );

                            SelectObject(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &context->PingGraphState.Text->sr,
                                &NormalGraphTextMargin, &NormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y;
                        PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorCpuKernel"), 0);
                        PhGraphStateGetDrawInfo(&context->PingGraphState, getDrawInfo, context->PingHistory.Count);

                        if (!context->PingGraphState.Valid)
                        {
                            ULONG i;
                            FLOAT max = 0;

                            for (i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                FLOAT data1;

                                context->PingGraphState.Data1[i] = data1 = (FLOAT)PhGetItemCircularBuffer_ULONG(&context->PingHistory, i);

                                if (max < data1)
                                    max = data1;
                            }

                            // Minimum scaling of timeout (1000ms default).
                            if (max < (FLOAT)context->MaxPingTimeout)
                                max = (FLOAT)context->MaxPingTimeout;

                            // Scale the data.
                            PhDivideSinglesBySingle(
                                context->PingGraphState.Data1,
                                max,
                                drawInfo->LineDataCount
                                );

                            context->PingGraphState.Valid = TRUE;
                        }
                    }
                }
                break;
            case GCN_GETTOOLTIPTEXT:
                {
                    PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)lParam;

                    if (getTooltipText->Index < getTooltipText->TotalCount)
                    {
                        if (header->hwndFrom == context->PingGraphHandle)
                        {
                            if (context->PingGraphState.TooltipIndex != getTooltipText->Index)
                            {
                                ULONG pingMs = PhGetItemCircularBuffer_ULONG(&context->PingHistory, getTooltipText->Index);

                                PhMoveReference(&context->PingGraphState.TooltipText,
                                    PhFormatString(L"Ping: %lums", pingMs)
                                    );
                            }

                            getTooltipText->Text = context->PingGraphState.TooltipText->sr;
                        }
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

NTSTATUS PhNetworkPingDialogThreadStart(
    _In_ PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    HWND windowHandle;
    PH_AUTO_POOL autoPool;

    PhInitializeAutoPool(&autoPool);

    windowHandle = CreateDialogParam(
        (HINSTANCE)PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_PINGDIALOG),
        NULL,
        NetworkPingWndProc,
        (LPARAM)Parameter
        );

    ShowWindow(windowHandle, SW_SHOW);
    SetForegroundWindow(windowHandle);

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!IsDialogMessage(windowHandle, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);

    return STATUS_SUCCESS;
}