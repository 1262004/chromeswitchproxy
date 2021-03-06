/* ***** BEGIN LICENSE BLOCK *****
* Copyright 2009 - 2010 Wenzhang Zhu (wzzhu@cs.hku.hk)
* Version: MPL 1.1/GPL 2.0/LGPL 2.1
*
* The contents of this file are subject to the Mozilla Public License Version
* 1.1 (the "License"); you may not use this file except in compliance with
* the License. You may obtain a copy of the License at
* http://www.mozilla.org/MPL/
*
* Software distributed under the License is distributed on an "AS IS" basis,
* WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
* for the specific language governing rights and limitations under the
* License.
* ***** END LICENSE BLOCK ***** */

#include "winproxy.h"

#include <windows.h>
#include <wininet.h>

#include "../npswitchproxy.h"
#include "../proxy_config.h"

WinProxy::WinProxy() {
}

WinProxy::~WinProxy() {
  if (hWinInetLib_ != NULL) {
    FreeLibrary(hWinInetLib_);
    hWinInetLib_ = NULL;
  }
}

// Converts the wide null-terminated string to utf8 null-terminated string.
// Note that the return value should be freed after use.
char* WinProxy::WStrToUtf8(LPCWSTR str) {
  int bufferSize = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)str, -1, NULL, 0, NULL, NULL);
	char* m = new char[bufferSize];
	WideCharToMultiByte(CP_UTF8, 0, str, -1, m, bufferSize, NULL, NULL);
	return m;
}

LPWSTR WinProxy::Utf8ToWStr(const char * str) {
  int bufferSize = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)str, -1, NULL, 0);
  wchar_t *m = new wchar_t[bufferSize];
  MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)str, -1, m, bufferSize);
  return m;
}

bool WinProxy::PlatformDependentStartup() {
  hWinInetLib_ = LoadLibrary(TEXT("wininet.dll"));
  if (hWinInetLib_ == NULL) {
    return false;
  }
  pInternetQueryOption_ = (InternetQueryOptionFunc)GetProcAddress(
      HMODULE(hWinInetLib_), "InternetQueryOptionW");
  if (pInternetQueryOption_ == NULL) {
    pInternetQueryOption_ = (InternetQueryOptionFunc)GetProcAddress(
        HMODULE(hWinInetLib_), "InternetQueryOptionA");
  }
  if (pInternetQueryOption_ == NULL) {
    FreeLibrary(hWinInetLib_);
    return false;
  }
  pInternetSetOption_ = (InternetSetOptionFunc)GetProcAddress(
      HMODULE(hWinInetLib_), "InternetSetOptionW");

  if (pInternetSetOption_ == NULL) {
    pInternetSetOption_ = (InternetSetOptionFunc)GetProcAddress(
        HMODULE(hWinInetLib_), "InternetSetOptionA");
  }
  if (pInternetSetOption_ == NULL) {
    FreeLibrary(hWinInetLib_);
    return false;
  }

  if (pInternetGetConnectedStateEx_ == NULL) {
    pInternetGetConnectedStateEx_ =
        (InternetGetConnectedStateExFunc)GetProcAddress(
            HMODULE(hWinInetLib_), "InternetGetConnectedStateExW");
  }
  if (pInternetGetConnectedStateEx_ == NULL) {
    pInternetGetConnectedStateEx_ =
        (InternetGetConnectedStateExFunc)GetProcAddress(
            HMODULE(hWinInetLib_), "InternetGetConnectedStateExA");
  }
  if (pInternetGetConnectedStateEx_ == NULL) {
    FreeLibrary(hWinInetLib_);
    return false;
  }
  return true;
}

void WinProxy::PlatformDependentShutdown() {
  if (hWinInetLib_ != NULL) {
    FreeLibrary(hWinInetLib_);
    hWinInetLib_ = NULL;
  }
}

// Gets current Inet connection name.
// Return value represents the connection status.
// If offline, connection_name is set to NULL and return value is false.
// If online, connection name is set to current connection name in widechar
// format so that the subsequence call can use this to set the name for
// Inet APIs. If it is a LAN connection, set connection_name to NULL
// as required by the Inet APIs. But its return value is also true.
bool WinProxy::GetActiveConnectionName(const void** connection_name) {
  static const int kMaxLengthOfConnectionName = 1024;
  wchar_t *buf = new wchar_t[kMaxLengthOfConnectionName];  
  DWORD flags;  
  if (pInternetGetConnectedStateEx_(
      &flags, (LPTSTR)buf, kMaxLengthOfConnectionName, NULL)) {
    if (flags & INTERNET_CONNECTION_OFFLINE) {
      delete buf;
      *connection_name = NULL;
      return false;
    }
    if (flags & INTERNET_CONNECTION_LAN) {
      // NULL to denote LAN.
      *connection_name = NULL;
    } else {
      *connection_name = (const char*)buf;
    }
    return true;
  }
  delete buf;
  return false;
}


bool WinProxy::GetProxyConfig(ProxyConfig* config) {
  INTERNET_PER_CONN_OPTION options[] = {
    {INTERNET_PER_CONN_FLAGS, 0},
    {INTERNET_PER_CONN_AUTOCONFIG_URL, 0},
    {INTERNET_PER_CONN_PROXY_SERVER, 0},
    {INTERNET_PER_CONN_PROXY_BYPASS, 0},    
  };
  INTERNET_PER_CONN_OPTION_LIST list;     
  unsigned long nSize = sizeof INTERNET_PER_CONN_OPTION_LIST;
  if (!GetActiveConnectionName((const void**)&list.pszConnection)) {
    return false;
  }
  list.dwSize = nSize;  
  list.pOptions = options;
  list.dwOptionCount = sizeof options / sizeof INTERNET_PER_CONN_OPTION;
  list.dwOptionError = 0;
  if (pInternetQueryOption_(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION,
                            &list, &nSize)) {
    memset(config, 0, sizeof(ProxyConfig));
    DWORD conn_flag = options[0].Value.dwValue;
    config->auto_detect = (conn_flag & PROXY_TYPE_AUTO_DETECT) ==
                          PROXY_TYPE_AUTO_DETECT;
    config->auto_config = (conn_flag & PROXY_TYPE_AUTO_PROXY_URL) ==
                          PROXY_TYPE_AUTO_PROXY_URL;
    config->use_proxy = (conn_flag & PROXY_TYPE_PROXY) == PROXY_TYPE_PROXY;

    if (options[1].Value.pszValue != NULL) {
      config->auto_config_url = WStrToUtf8(options[1].Value.pszValue);
      GlobalFree(options[1].Value.pszValue);
    }
    if (options[2].Value.pszValue != NULL) {
      config->proxy_server = WStrToUtf8(options[2].Value.pszValue);
      GlobalFree(options[2].Value.pszValue);
    }
    if (options[3].Value.pszValue != NULL) {
      config->bypass_list = WStrToUtf8(options[3].Value.pszValue);
      GlobalFree(options[3].Value.pszValue);
    }
    DebugLog("npswitchproxy: InternetGetOption succeeded.\n");
    return true;
  }
  DebugLog("npswitchproxy: InternetGetOption failed.\n");
  return false;
}

bool WinProxy::SetProxyConfig(const ProxyConfig& config) {
  INTERNET_PER_CONN_OPTION options[] = {
    {INTERNET_PER_CONN_FLAGS, 0},
    {INTERNET_PER_CONN_AUTOCONFIG_URL, 0},    
    {INTERNET_PER_CONN_PROXY_SERVER, 0},
    {INTERNET_PER_CONN_PROXY_BYPASS, 0},    
  };
  INTERNET_PER_CONN_OPTION_LIST list;     
  unsigned long nSize = sizeof INTERNET_PER_CONN_OPTION_LIST;
  if (!GetActiveConnectionName((const void**)&list.pszConnection)) {
      return false;
  }
  list.dwSize = nSize;  
  list.pOptions = options;
  list.dwOptionCount = sizeof options / sizeof INTERNET_PER_CONN_OPTION;
  list.dwOptionError = 0;
  options[0].Value.dwValue = PROXY_TYPE_DIRECT;
  if (config.auto_config) {
    options[0].Value.dwValue |= PROXY_TYPE_AUTO_PROXY_URL;
  }
  if (config.auto_detect) {
    options[0].Value.dwValue |= PROXY_TYPE_AUTO_DETECT;
  }
  if (config.use_proxy) {
    options[0].Value.dwValue |= PROXY_TYPE_PROXY;
  }
  if (config.auto_config_url != NULL) {
    options[1].Value.pszValue = Utf8ToWStr(config.auto_config_url);
  }
  if (config.proxy_server != NULL) {
    options[2].Value.pszValue = Utf8ToWStr(config.proxy_server);
  }
  if (config.bypass_list != NULL) {
    options[3].Value.pszValue = Utf8ToWStr(config.bypass_list);
  }
  if (pInternetSetOption_(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION,
                         &list, nSize)) {
    DebugLog("npswitchproxy: InternetSetOption succeeded.\n");
    return true;
  }
  DWORD dw = GetLastError();
  DebugLog("npswitchproxy: InternetSetOption failed.\n");
  return false;
}