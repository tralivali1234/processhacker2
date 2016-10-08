/*
 * Process Hacker Network Tools -
 *   Tracert dialog
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

#define MAX_PINGS  4
#define IP_ADDRESS_COLUMN (MAX_PINGS + 1)
#define HOSTNAME_COLUMN (MAX_PINGS + 2)

#define DEFAULT_MAXIMUM_HOPS        30 
#define DEFAULT_SEND_SIZE           64 
#define DEFAULT_RECEIVE_SIZE      ((sizeof(ICMP_ECHO_REPLY) + DEFAULT_SEND_SIZE + MAX_OPT_SIZE)) 
#define DEFAULT_TIMEOUT 5000
#define MIN_INTERVAL    500 //1000

typedef struct _TRACERT_ERROR
{
    INT LvItemIndex;
    INT lvSubItemIndex;
    ULONG LastErrorCode;
} TRACERT_ERROR, *PTRACERT_ERROR;

typedef struct _TRACERT_RESOLVE_WORKITEM
{
    HWND WindowHandle;
    INT LvItemIndex;
    ULONG Type;
    SOCKADDR_STORAGE SocketAddress;
    WCHAR SocketAddressHostname[NI_MAXHOST];
} TRACERT_RESOLVE_WORKITEM, *PTRACERT_RESOLVE_WORKITEM;



PPH_STRING TracertGetErrorMessage(
    _In_ IP_STATUS Result
    )
{
    PPH_STRING message;
    ULONG messageLength = 0;

    if (GetIpErrorString(Result, NULL, &messageLength) == ERROR_INSUFFICIENT_BUFFER)
    {
        message = PhCreateStringEx(NULL, messageLength * sizeof(WCHAR));

        if (GetIpErrorString(Result, message->Buffer, &messageLength) != ERROR_SUCCESS)
        {
            PhDereferenceObject(message);
            message = PhGetWin32Message(Result);
        }
    }
    else
    {
        message = PhGetWin32Message(Result);
    }

    return message;
}

VOID TracertDelayExecution(
    _In_ ULONG Milliseconds
    )
{
    LARGE_INTEGER interval;

    NtDelayExecution(FALSE, PhTimeoutFromMilliseconds(&interval, Milliseconds));
}

VOID TracertAppendText(
    _In_ PNETWORK_OUTPUT_CONTEXT Context,
    _In_ INT Index,
    _In_ INT SubItemIndex,
    _In_ PWSTR Text
    )
{
    WCHAR itemText[MAX_PATH] = L"";

    ListView_GetItemText(
        Context->OutputHandle, 
        Index, 
        SubItemIndex,
        itemText,
        ARRAYSIZE(itemText)
        );

    if (PhCountStringZ(itemText) > 0)
    {
        if (!wcsstr(itemText, Text))
        {
            PPH_STRING string;

            string = PhFormatString(L"%s, %s", itemText, Text);

            PhSetListViewSubItem(
                Context->OutputHandle,
                Index,
                SubItemIndex,
                string->Buffer
                );
            PhDereferenceObject(string);
        }
    }
    else
    {
        PhSetListViewSubItem(
            Context->OutputHandle,
            Index,
            SubItemIndex,
            Text
            );
    }
}

static VOID TracertUpdateTime(
    _In_ PNETWORK_OUTPUT_CONTEXT Context,
    _In_ INT Index,
    _In_ INT SubIndex,
    _In_ ULONG RoundTripTime
    ) 
{ 
    if (RoundTripTime)
    { 
        PhSetListViewSubItem(Context->OutputHandle, Index, SubIndex, PhaFormatString(L"%lu ms", RoundTripTime)->Buffer);
    } 
    else 
    { 
        PhSetListViewSubItem(Context->OutputHandle, Index, SubIndex, PhaFormatString(L"<1 ms", RoundTripTime)->Buffer);
    } 
} 

static NTSTATUS TracertHostnameLookupCallback(
    _In_ PVOID Parameter
    )
{
    WSADATA wsa;
    PTRACERT_RESOLVE_WORKITEM workItem = Parameter;

    if (WSAStartup(WINSOCK_VERSION, &wsa) != ERROR_SUCCESS)
    {
        return STATUS_UNEXPECTED_NETWORK_ERROR;
    }

    if (workItem->Type == PH_IPV4_NETWORK_TYPE)
    {
        if (!GetNameInfo(
            (PSOCKADDR)&workItem->SocketAddress,
            sizeof(SOCKADDR_IN),
            workItem->SocketAddressHostname,
            sizeof(workItem->SocketAddressHostname),
            NULL,
            0,
            NI_NAMEREQD
            ))
        {
            PostMessage(workItem->WindowHandle, NTM_RECEIVEDTRACE, 0, (LPARAM)workItem);
        }
        else
        {
            ULONG errorCode = WSAGetLastError();

            if (errorCode != WSAHOST_NOT_FOUND && errorCode != WSATRY_AGAIN)
            {
                //PPH_STRING errorMessage = PhGetWin32Message(errorCode);
                //PhSetListViewSubItem(workItem->LvHandle, workItem->LvItemIndex, HOSTNAME_COLUMN, errorMessage->Buffer);
                //PhDereferenceObject(errorMessage);
            }
        }
    }
    else if (workItem->Type == PH_IPV6_NETWORK_TYPE)
    {
        if (!GetNameInfo(
            (PSOCKADDR)&workItem->SocketAddress,
            sizeof(SOCKADDR_IN6),
            workItem->SocketAddressHostname,
            sizeof(workItem->SocketAddressHostname),
            NULL,
            0,
            NI_NAMEREQD
            ))
        {
            PostMessage(workItem->WindowHandle, NTM_RECEIVEDTRACE, 0, (LPARAM)workItem);
            //PhSetListViewSubItem(workItem->LvHandle, workItem->LvItemIndex, HOSTNAME_COLUMN, ipAddressHostname);
        }
        else
        {
            ULONG errorCode = WSAGetLastError();

            if (errorCode != WSAHOST_NOT_FOUND && errorCode != WSATRY_AGAIN)
            {
                //PPH_STRING errorMessage = PhGetWin32Message(errorCode);
                //PhSetListViewSubItem(workItem->LvHandle, workItem->LvItemIndex, HOSTNAME_COLUMN, errorMessage->Buffer);
                //PhDereferenceObject(errorMessage);
            }
        }
    }

    WSACleanup();

    return STATUS_SUCCESS;
}

VOID TracertQueueHostLookup(
    _In_ PNETWORK_OUTPUT_CONTEXT Context,
    _In_ INT LvItemIndex,
    _In_ PVOID SocketAddress
    ) 
{
    if (Context->RemoteEndpoint.Address.Type == PH_IPV4_NETWORK_TYPE)
    {
        IN_ADDR sockAddrIn;
        PTRACERT_RESOLVE_WORKITEM work;
        ULONG addressStringLength = INET6_ADDRSTRLEN;
        WCHAR addressString[INET6_ADDRSTRLEN] = L"";

        memset(&sockAddrIn, 0, sizeof(IN_ADDR));
        memcpy(&sockAddrIn, SocketAddress, sizeof(IN_ADDR));

        if (NT_SUCCESS(RtlIpv4AddressToStringEx(&sockAddrIn, 0, addressString, &addressStringLength)))
        {
            TracertAppendText(
                Context, 
                LvItemIndex, 
                IP_ADDRESS_COLUMN, 
                addressString
                );
        }

        work = PhAllocate(sizeof(TRACERT_RESOLVE_WORKITEM));
        memset(work, 0, sizeof(TRACERT_RESOLVE_WORKITEM));

        work->Type = PH_IPV4_NETWORK_TYPE;
        work->WindowHandle = Context->WindowHandle;
        work->LvItemIndex = LvItemIndex;

        ((PSOCKADDR_IN)&work->SocketAddress)->sin_family = AF_INET;
        ((PSOCKADDR_IN)&work->SocketAddress)->sin_addr = sockAddrIn;

        PhQueueItemWorkQueue(PhGetGlobalWorkQueue(), TracertHostnameLookupCallback, work);
    }
    else if (Context->RemoteEndpoint.Address.Type == PH_IPV6_NETWORK_TYPE)
    {
        IN6_ADDR sockAddrIn6;
        PTRACERT_RESOLVE_WORKITEM work;
        ULONG addressStringLength = INET6_ADDRSTRLEN;
        WCHAR addressString[INET6_ADDRSTRLEN] = L"";

        memset(&sockAddrIn6, 0, sizeof(IN6_ADDR));
        memcpy(&sockAddrIn6, SocketAddress, sizeof(IN6_ADDR));
       
        //RtlIpv6AddressToString(&sockAddrIn6, addressString);
        if (NT_SUCCESS(RtlIpv6AddressToStringEx(&sockAddrIn6, 0, 0, addressString, &addressStringLength)))
        {
            TracertAppendText(
                Context,
                LvItemIndex,
                IP_ADDRESS_COLUMN,
                addressString
                );

            //PhSetListViewSubItem(Context->OutputHandle, LvItemIndex, IP_ADDRESS_COLUMN, addressString);
            //PhSetListViewSubItem(Context->OutputHandle, LvItemIndex, HOSTNAME_COLUMN, L"Resolving address...");
        }

        work = PhAllocate(sizeof(TRACERT_RESOLVE_WORKITEM));
        memset(work, 0, sizeof(TRACERT_RESOLVE_WORKITEM));

        work->Type = PH_IPV6_NETWORK_TYPE;
        work->WindowHandle = Context->WindowHandle;
        work->LvItemIndex = LvItemIndex;

        ((PSOCKADDR_IN6)&work->SocketAddress)->sin6_family = AF_INET6;
        ((PSOCKADDR_IN6)&work->SocketAddress)->sin6_addr = sockAddrIn6;

        PhQueueItemWorkQueue(PhGetGlobalWorkQueue(), TracertHostnameLookupCallback, work);
    }
}




NTSTATUS NetworkTracertThreadStart(
    _In_ PVOID Parameter
    )
{
    PNETWORK_OUTPUT_CONTEXT context;
    PH_AUTO_POOL autoPool;
    HANDLE icmpHandle = INVALID_HANDLE_VALUE;
    SOCKADDR_STORAGE sourceAddress = { 0 };
    SOCKADDR_STORAGE destinationAddress = { 0 };
    ULONG icmpReplyLength = 0;
    PVOID icmpReplyBuffer = NULL;
    PPH_BYTES icmpEchoBuffer = NULL;
    PPH_STRING icmpRandString = NULL;
    IP_OPTION_INFORMATION pingOptions =
    {
        1,
        0,
        IP_FLAG_DF,
        0
    };

    context = (PNETWORK_OUTPUT_CONTEXT)Parameter;

    PhInitializeAutoPool(&autoPool);

    if (icmpRandString = PhCreateStringEx(NULL, PhGetIntegerSetting(SETTING_NAME_PING_SIZE) * 2 + 2))
    {
        PhGenerateRandomAlphaString(icmpRandString->Buffer, (ULONG)icmpRandString->Length / sizeof(WCHAR));

        icmpEchoBuffer = PhConvertUtf16ToMultiByte(icmpRandString->Buffer);
        PhDereferenceObject(icmpRandString);
    }

    switch (context->RemoteEndpoint.Address.Type)
    {
    case PH_IPV4_NETWORK_TYPE:
        icmpHandle = IcmpCreateFile();
        break;
    case PH_IPV6_NETWORK_TYPE:
        icmpHandle = Icmp6CreateFile();
        break;
    }

    __try
    {
        if (icmpHandle == INVALID_HANDLE_VALUE)
            __leave;

        if (context->RemoteEndpoint.Address.Type == PH_IPV4_NETWORK_TYPE)
        {
            ((PSOCKADDR_IN)&destinationAddress)->sin_family = AF_INET;
            ((PSOCKADDR_IN)&destinationAddress)->sin_addr = context->RemoteEndpoint.Address.InAddr;
            //((PSOCKADDR_IN)&destinationAddress)->sin_port = (USHORT)context->RemoteEndpoint.Port;//_byteswap_ushort((USHORT)Context->RemoteEndpoint.Port);
        }
        else if (context->RemoteEndpoint.Address.Type == PH_IPV6_NETWORK_TYPE)
        {
            ((PSOCKADDR_IN6)&destinationAddress)->sin6_family = AF_INET6;
            ((PSOCKADDR_IN6)&destinationAddress)->sin6_addr = context->RemoteEndpoint.Address.In6Addr;
            //((PSOCKADDR_IN6)&destinationAddress)->sin6_port = (USHORT)context->RemoteEndpoint.Port;//_byteswap_ushort((USHORT)Context->RemoteEndpoint.Port);
        }

        for (UINT i = 0; i < DEFAULT_MAXIMUM_HOPS; i++)
        {
            IN_ADDR last4ReplyAddress = in4addr_any;
            IN6_ADDR last6ReplyAddress = in6addr_any;
            INT lvItemIndex;

            if (!context->OutputHandle)
                break;

            lvItemIndex = PhAddListViewItem(
                context->OutputHandle,
                MAXINT,
                PhaFormatString(L"%u", (UINT)pingOptions.Ttl)->Buffer,
                NULL
                );

            for (UINT i = 0; i < MAX_PINGS; i++)
            {
                if (!context->OutputHandle)
                    break;

                if (context->RemoteEndpoint.Address.Type == PH_IPV4_NETWORK_TYPE)
                {
                    // Allocate ICMPv6 message.
                    icmpReplyLength = ICMP_BUFFER_SIZE(sizeof(ICMP_ECHO_REPLY), icmpEchoBuffer);
                    icmpReplyBuffer = PhAllocate(icmpReplyLength);
                    memset(icmpReplyBuffer, 0, icmpReplyLength);

                    if (!IcmpSendEcho2Ex(
                        icmpHandle,
                        0,
                        NULL,
                        NULL,
                        ((PSOCKADDR_IN)&sourceAddress)->sin_addr.s_addr,
                        ((PSOCKADDR_IN)&destinationAddress)->sin_addr.s_addr,
                        icmpEchoBuffer->Buffer,
                        (USHORT)icmpEchoBuffer->Length,
                        &pingOptions,
                        icmpReplyBuffer,
                        icmpReplyLength,
                        DEFAULT_TIMEOUT
                        ))
                    {
                        PTRACERT_ERROR error;
                        
                        error = PhAllocate(sizeof(TRACERT_ERROR));
                        memset(error, 0, sizeof(TRACERT_ERROR));

                        error->LastErrorCode = GetLastError();
                        error->LvItemIndex = lvItemIndex;
                        error->lvSubItemIndex = i + 1;

                        PostMessage(context->WindowHandle, WM_TRACERT_ERROR, 0, (LPARAM)error);
                    }
                    else
                    {
                        PICMP_ECHO_REPLY reply4 = (PICMP_ECHO_REPLY)icmpReplyBuffer;

                        TracertUpdateTime(
                            context, 
                            lvItemIndex, 
                            i + 1, 
                            reply4->RoundTripTime
                            );

                        TracertQueueHostLookup(
                            context,
                            lvItemIndex,
                            &reply4->Address
                            );

                        memcpy(&last4ReplyAddress, &reply4->Address, sizeof(IN_ADDR));

                        if (reply4->Status == IP_HOP_LIMIT_EXCEEDED)
                        {
                            if (reply4->RoundTripTime < MIN_INTERVAL)
                            {
                                TracertDelayExecution(MIN_INTERVAL - reply4->RoundTripTime);
                            }
                        }
                        else if (reply4->Status != IP_SUCCESS)
                        {
                            PTRACERT_ERROR error;

                            error = PhAllocate(sizeof(TRACERT_ERROR));
                            memset(error, 0, sizeof(TRACERT_ERROR));

                            error->LastErrorCode = reply4->Status;
                            error->LvItemIndex = lvItemIndex;
                            error->lvSubItemIndex = i + 1;

                            PostMessage(context->WindowHandle, WM_TRACERT_ERROR, 0, (LPARAM)error);
                        }
                    }

                    PhFree(icmpReplyBuffer);
                }
                else
                {
                    icmpReplyLength = ICMP_BUFFER_SIZE(sizeof(ICMPV6_ECHO_REPLY), icmpEchoBuffer);
                    icmpReplyBuffer = PhAllocate(icmpReplyLength);
                    memset(icmpReplyBuffer, 0, icmpReplyLength);

                    if (!Icmp6SendEcho2(
                        icmpHandle,
                        0,
                        NULL,
                        NULL,
                        ((PSOCKADDR_IN6)&sourceAddress),
                        ((PSOCKADDR_IN6)&destinationAddress),
                        icmpEchoBuffer->Buffer,
                        (USHORT)icmpEchoBuffer->Length,
                        &pingOptions,
                        icmpReplyBuffer,
                        icmpReplyLength,
                        DEFAULT_TIMEOUT
                        ))
                    {
                        PTRACERT_ERROR error;

                        error = PhAllocate(sizeof(TRACERT_ERROR));
                        memset(error, 0, sizeof(TRACERT_ERROR));

                        error->LastErrorCode = GetLastError();
                        error->LvItemIndex = lvItemIndex;
                        error->lvSubItemIndex = i + 1;

                        PostMessage(context->WindowHandle, WM_TRACERT_ERROR, 0, (LPARAM)error);
                    }
                    else
                    {
                        PICMPV6_ECHO_REPLY reply6 = (PICMPV6_ECHO_REPLY)icmpReplyBuffer;

                        TracertUpdateTime(
                            context, 
                            lvItemIndex, 
                            i + 1, 
                            reply6->RoundTripTime
                            );

                        TracertQueueHostLookup(
                            context,
                            lvItemIndex,
                            &reply6->Address.sin6_addr
                            );

                        memcpy(&last6ReplyAddress, &reply6->Address.sin6_addr, sizeof(IN6_ADDR));

                        if (reply6->Status == IP_HOP_LIMIT_EXCEEDED)
                        {
                            if (reply6->RoundTripTime < MIN_INTERVAL)
                            {
                                TracertDelayExecution(MIN_INTERVAL - reply6->RoundTripTime);
                            }
                        }
                        else if (reply6->Status != IP_SUCCESS)
                        {
                            PTRACERT_ERROR error;

                            error = PhAllocate(sizeof(TRACERT_ERROR));
                            memset(error, 0, sizeof(TRACERT_ERROR));

                            error->LastErrorCode = reply6->Status;
                            error->LvItemIndex = lvItemIndex;
                            error->lvSubItemIndex = i + 1;

                            PostMessage(context->WindowHandle, WM_TRACERT_ERROR, 0, (LPARAM)error);
                        }
                    }

                    PhFree(icmpReplyBuffer);
                }
            }

            if (context->RemoteEndpoint.Address.Type == PH_IPV4_NETWORK_TYPE)
            {
                if (!memcmp(&last4ReplyAddress, &((PSOCKADDR_IN)&destinationAddress)->sin_addr, sizeof(IN_ADDR)))
                    break;
            }
            else if (context->RemoteEndpoint.Address.Type == PH_IPV6_NETWORK_TYPE)
            {
                if (!memcmp(&last6ReplyAddress, &((PSOCKADDR_IN6)&destinationAddress)->sin6_addr, sizeof(IN6_ADDR)))
                    break;
            }

            pingOptions.Ttl++;
        }
    }
    __finally
    {
        if (icmpHandle != INVALID_HANDLE_VALUE)
        {
            IcmpCloseHandle(icmpHandle);
        }
    }

    PhDeleteAutoPool(&autoPool);

    PostMessage(context->WindowHandle, NTM_RECEIVEDFINISH, 0, 0);

    return STATUS_SUCCESS;
}



INT_PTR CALLBACK TracertDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PNETWORK_OUTPUT_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PNETWORK_OUTPUT_CONTEXT)lParam;
        SetProp(hwndDlg, L"Context", (HANDLE)context);
    }
    else
    {
        context = (PNETWORK_OUTPUT_CONTEXT)GetProp(hwndDlg, L"Context");

        if (uMsg == WM_DESTROY)
        {
            PhSaveListViewColumnsToSetting(SETTING_NAME_TRACERT_COLUMNS, context->OutputHandle);
            PhSaveWindowPlacementToSetting(SETTING_NAME_TRACERT_WINDOW_POSITION, SETTING_NAME_TRACERT_WINDOW_SIZE, hwndDlg);

            if (context->FontHandle)
                DeleteObject(context->FontHandle);

            PhDeleteLayoutManager(&context->LayoutManager);

            context->OutputHandle = NULL;

            RemoveProp(hwndDlg, L"Context");
            PhFree(context);

            PostQuitMessage(0);
        }
    }

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            HANDLE dialogThread;
            
            PhCenterWindow(hwndDlg, GetParent(hwndDlg));
            CommonSetWindowIcon(hwndDlg);

            Static_SetText(hwndDlg,
                PhaFormatString(L"Tracing  %s...", context->IpAddressString)->Buffer
                );
            Static_SetText(GetDlgItem(hwndDlg, IDC_STATUS),
                PhaFormatString(L"Tracing route to %s with %lu bytes of data...", context->IpAddressString, PhGetIntegerSetting(SETTING_NAME_PING_SIZE))->Buffer
                );

            context->WindowHandle = hwndDlg;
            context->OutputHandle = GetDlgItem(hwndDlg, IDC_LIST_TRACERT);
            context->MaxPingTimeout = PhGetIntegerSetting(SETTING_NAME_PING_MINIMUM_SCALING);
            context->FontHandle = CommonCreateFont(-15, GetDlgItem(hwndDlg, IDC_STATUS));

            PhSetListViewStyle(context->OutputHandle, FALSE, TRUE);
            PhSetControlTheme(context->OutputHandle, L"explorer");
            PhAddListViewColumn(context->OutputHandle, 0, 0, 0, LVCFMT_RIGHT, 30, L"TTL");

            for (UINT i = 0; i < MAX_PINGS; i++)
                PhAddListViewColumn(context->OutputHandle, i + 1, i + 1, i + 1, LVCFMT_RIGHT, 50, L"Time");

            PhAddListViewColumn(context->OutputHandle, IP_ADDRESS_COLUMN, IP_ADDRESS_COLUMN, IP_ADDRESS_COLUMN, LVCFMT_LEFT, 120, L"IP Address");
            PhAddListViewColumn(context->OutputHandle, HOSTNAME_COLUMN, HOSTNAME_COLUMN, HOSTNAME_COLUMN, LVCFMT_LEFT, 240, L"Hostname");
            PhLoadListViewColumnsFromSetting(SETTING_NAME_TRACERT_COLUMNS, context->OutputHandle);
            PhSetExtendedListView(context->OutputHandle);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->OutputHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_STATUS), NULL, PH_ANCHOR_TOP | PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT);      
            PhLoadWindowPlacementFromSetting(SETTING_NAME_TRACERT_WINDOW_POSITION, SETTING_NAME_TRACERT_WINDOW_SIZE, hwndDlg);

            if (dialogThread = PhCreateThread(0, NetworkTracertThreadStart, (PVOID)context))
                NtClose(dialogThread);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                DestroyWindow(hwndDlg);
                break;
            }
        }
        break;
    case WM_SIZE:
        PhLayoutManagerLayout(&context->LayoutManager);
        break;
    case WM_TRACERT_ERROR:
        {
            PTRACERT_ERROR error = (PTRACERT_ERROR)lParam;

            if (error->LastErrorCode == IP_REQ_TIMED_OUT)
            {
                PhSetListViewSubItem(
                    context->OutputHandle, 
                    error->LvItemIndex, 
                    error->lvSubItemIndex, 
                    L"*"
                    );

                //TracertAppendText(
                //    context, 
                //    error->LvItemIndex, 
                //    IP_ADDRESS_COLUMN, 
                //    TracertGetErrorMessage(error->LastErrorCode)->Buffer
                //    );
            }
            else
            {
                PPH_STRING errorMessage;

                errorMessage = PH_AUTO(TracertGetErrorMessage(error->LastErrorCode));

                TracertAppendText(
                    context, 
                    error->LvItemIndex, 
                    IP_ADDRESS_COLUMN, 
                    errorMessage->Buffer
                    );
            }

            PhFree(error);
        }
        break;
    case NTM_RECEIVEDTRACE:
        {
            PTRACERT_RESOLVE_WORKITEM workItem = (PTRACERT_RESOLVE_WORKITEM)lParam;

            TracertAppendText(
                context,
                workItem->LvItemIndex,
                HOSTNAME_COLUMN,
                workItem->SocketAddressHostname
                );

            PhFree(workItem);
        }
        break;
    case NTM_RECEIVEDFINISH:
        {
            PPH_STRING windowText;

            if (windowText = PH_AUTO(PhGetWindowText(context->WindowHandle)))
            {
                Static_SetText(
                    context->WindowHandle,
                    PhaFormatString(L"%s complete.", windowText->Buffer)->Buffer
                    );
            }

            if (windowText = PH_AUTO(PhGetWindowText(GetDlgItem(hwndDlg, IDC_STATUS))))
            {
                Static_SetText(
                    GetDlgItem(hwndDlg, IDC_STATUS),
                    PhaFormatString(L"%s complete.", windowText->Buffer)->Buffer
                    );
            }
        }
        break;
    }

    return FALSE;
}

NTSTATUS TracertDialogThreadStart(
    _In_ PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    HWND windowHandle;
    PH_AUTO_POOL autoPool;
    PNETWORK_OUTPUT_CONTEXT context = (PNETWORK_OUTPUT_CONTEXT)Parameter;

    PhInitializeAutoPool(&autoPool);

    windowHandle = CreateDialogParam(
        (HINSTANCE)PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_TRACERT),
        NULL,
        TracertDlgProc,
        (LPARAM)Parameter
        );

    ShowWindow(windowHandle, SW_SHOW);
    SetForegroundWindow(windowHandle);

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!IsDialogMessage(context->WindowHandle, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);

    return STATUS_SUCCESS;
}

VOID ShowTracertWindow(
    _In_ PPH_NETWORK_ITEM NetworkItem
    )
{
    HANDLE dialogThread;
    PNETWORK_OUTPUT_CONTEXT context;

    context = (PNETWORK_OUTPUT_CONTEXT)PhAllocate(sizeof(NETWORK_OUTPUT_CONTEXT));
    memset(context, 0, sizeof(NETWORK_OUTPUT_CONTEXT));

    context->RemoteEndpoint = NetworkItem->RemoteEndpoint;

    if (NetworkItem->RemoteEndpoint.Address.Type == PH_IPV4_NETWORK_TYPE)
    {
        RtlIpv4AddressToString(&NetworkItem->RemoteEndpoint.Address.InAddr, context->IpAddressString);
    }
    else if (NetworkItem->RemoteEndpoint.Address.Type == PH_IPV6_NETWORK_TYPE)
    {
        RtlIpv6AddressToString(&NetworkItem->RemoteEndpoint.Address.In6Addr, context->IpAddressString);
    }

    if (dialogThread = PhCreateThread(0, TracertDialogThreadStart, (PVOID)context))
    {
        NtClose(dialogThread);
    }
}

VOID ShowTracertWindowFromAddress(
    _In_ PH_IP_ENDPOINT RemoteEndpoint
    )
{
    HANDLE dialogThread;
    PNETWORK_OUTPUT_CONTEXT context;

    context = (PNETWORK_OUTPUT_CONTEXT)PhAllocate(sizeof(NETWORK_OUTPUT_CONTEXT));
    memset(context, 0, sizeof(NETWORK_OUTPUT_CONTEXT));

    context->RemoteEndpoint = RemoteEndpoint;

    if (RemoteEndpoint.Address.Type == PH_IPV4_NETWORK_TYPE)
    {
        RtlIpv4AddressToString(&RemoteEndpoint.Address.InAddr, context->IpAddressString);
    }
    else if (RemoteEndpoint.Address.Type == PH_IPV6_NETWORK_TYPE)
    {
        RtlIpv6AddressToString(&RemoteEndpoint.Address.In6Addr, context->IpAddressString);
    }

    if (dialogThread = PhCreateThread(0, TracertDialogThreadStart, (PVOID)context))
    {
        NtClose(dialogThread);
    }
}