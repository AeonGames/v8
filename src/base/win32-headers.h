// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_WIN32_HEADERS_H_
#define V8_BASE_WIN32_HEADERS_H_

#ifndef WIN32_LEAN_AND_MEAN
// WIN32_LEAN_AND_MEAN implies NOCRYPT and NOGDI.
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOKERNEL
#define NOKERNEL
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOSERVICE
#define NOSERVICE
#endif
#ifndef NOSOUND
#define NOSOUND
#endif
#ifndef NOMCX
#define NOMCX
#endif
// Require Windows Vista or higher (this is required for the
// QueryThreadCycleTime function to be present).
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>

#include <mmsystem.h>  // For timeGetTime().
#include <signal.h>  // For raise().
#include <time.h>  // For LocalOffset() implementation.
#include <dbghelp.h>         // For SymLoadModule64 and al.
#include <errno.h>           // For STRUNCATE
#include <versionhelpers.h>  // For IsWindows8OrGreater().
#include <limits.h>  // For INT_MAX and al.
#include <tlhelp32.h>  // For Module32First and al.

// These additional WIN32 includes have to be right here as the #undef's below
// makes it impossible to have them elsewhere.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#include <process.h>  // For _beginthreadex().
#include <stdlib.h>

#undef VOID
#undef DELETE
#undef IN
#undef THIS
#undef CONST
#undef NAN
#undef UNKNOWN
#undef NONE
#undef ANY
#undef IGNORE
#undef STRICT
#undef GetObject
#undef CreateSemaphore
#undef Yield
#undef RotateRight32
#undef RotateLeft32
#undef RotateRight64
#undef RotateLeft64
#undef __deref
#endif  // V8_BASE_WIN32_HEADERS_H_
