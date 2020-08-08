// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for Win32.

#include <limits>

#include "src/base/win32-headers.h"

#if defined(__MINGW32__) || defined(__MINGW64__)
// REMOVE ONCE mingw functions.h file is fixed
#undef __deref
#endif
#include "src/base/bits.h"
#include "src/base/lazy-instance.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/timezone-cache.h"
#include "src/base/utils/random-number-generator.h"

#include <VersionHelpers.h>

#if defined(_MSC_VER)
#include <crtdbg.h>  // NOLINT
#endif               // defined(_MSC_VER)

namespace v8 {
namespace base {

namespace {

bool g_hard_abort = false;

}  // namespace

class WindowsTimezoneCache : public TimezoneCache {
 public:
  WindowsTimezoneCache() : initialized_(false) {}

  ~WindowsTimezoneCache() override {}

  void Clear(TimeZoneDetection) override { initialized_ = false; }

  const char* LocalTimezone(double time) override;

  double LocalTimeOffset(double time, bool is_utc) override;

  double DaylightSavingsOffset(double time) override;

  // Initialize timezone information. The timezone information is obtained from
  // windows. If we cannot get the timezone information we fall back to CET.
  void InitializeIfNeeded() {
    // Just return if timezone information has already been initialized.
    if (initialized_) return;

    // Initialize POSIX time zone data.
    _tzset();
    // Obtain timezone information from operating system.
    memset(&tzinfo_, 0, sizeof(tzinfo_));
    if (GetTimeZoneInformation(&tzinfo_) == TIME_ZONE_ID_INVALID) {
      // If we cannot get timezone information we fall back to CET.
      tzinfo_.Bias = -60;
      tzinfo_.StandardDate.wMonth = 10;
      tzinfo_.StandardDate.wDay = 5;
      tzinfo_.StandardDate.wHour = 3;
      tzinfo_.StandardBias = 0;
      tzinfo_.DaylightDate.wMonth = 3;
      tzinfo_.DaylightDate.wDay = 5;
      tzinfo_.DaylightDate.wHour = 2;
      tzinfo_.DaylightBias = -60;
    }

    // Make standard and DST timezone names.
    WideCharToMultiByte(CP_UTF8, 0, tzinfo_.StandardName, -1, std_tz_name_,
                        kTzNameSize, nullptr, nullptr);
    std_tz_name_[kTzNameSize - 1] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, tzinfo_.DaylightName, -1, dst_tz_name_,
                        kTzNameSize, nullptr, nullptr);
    dst_tz_name_[kTzNameSize - 1] = '\0';

    // If OS returned empty string or resource id (like "@tzres.dll,-211")
    // simply guess the name from the UTC bias of the timezone.
    // To properly resolve the resource identifier requires a library load,
    // which is not possible in a sandbox.
    if (std_tz_name_[0] == '\0' || std_tz_name_[0] == '@') {
      OS::SNPrintF(std_tz_name_, kTzNameSize - 1,
                   "%s Standard Time",
                   GuessTimezoneNameFromBias(tzinfo_.Bias));
    }
    if (dst_tz_name_[0] == '\0' || dst_tz_name_[0] == '@') {
      OS::SNPrintF(dst_tz_name_, kTzNameSize - 1,
                   "%s Daylight Time",
                   GuessTimezoneNameFromBias(tzinfo_.Bias));
    }
    // Timezone information initialized.
    initialized_ = true;
  }

  // Guess the name of the timezone from the bias.
  // The guess is very biased towards the northern hemisphere.
  const char* GuessTimezoneNameFromBias(int bias) {
    static const int kHour = 60;
    switch (-bias) {
      case -9*kHour: return "Alaska";
      case -8*kHour: return "Pacific";
      case -7*kHour: return "Mountain";
      case -6*kHour: return "Central";
      case -5*kHour: return "Eastern";
      case -4*kHour: return "Atlantic";
      case  0*kHour: return "GMT";
      case +1*kHour: return "Central Europe";
      case +2*kHour: return "Eastern Europe";
      case +3*kHour: return "Russia";
      case +5*kHour + 30: return "India";
      case +8*kHour: return "China";
      case +9*kHour: return "Japan";
      case +12*kHour: return "New Zealand";
      default: return "Local";
    }
  }


 private:
  static const int kTzNameSize = 128;
  bool initialized_;
  char std_tz_name_[kTzNameSize];
  char dst_tz_name_[kTzNameSize];
  TIME_ZONE_INFORMATION tzinfo_;
  friend class Win32Time;
};


// ----------------------------------------------------------------------------
// The Time class represents time on win32. A timestamp is represented as
// a 64-bit integer in 100 nanoseconds since January 1, 1601 (UTC). JavaScript
// timestamps are represented as a doubles in milliseconds since 00:00:00 UTC,
// January 1, 1970.

class Win32Time {
 public:
  // Constructors.
  Win32Time();
  explicit Win32Time(double jstime);
  Win32Time(int year, int mon, int day, int hour, int min, int sec);

  // Convert timestamp to JavaScript representation.
  double ToJSTime();

  // Set timestamp to current time.
  void SetToCurrentTime();

  // Returns the local timezone offset in milliseconds east of UTC. This is
  // the number of milliseconds you must add to UTC to get local time, i.e.
  // LocalOffset(CET) = 3600000 and LocalOffset(PST) = -28800000. This
  // routine also takes into account whether daylight saving is effect
  // at the time.
  int64_t LocalOffset(WindowsTimezoneCache* cache);

  // Returns the daylight savings time offset for the time in milliseconds.
  int64_t DaylightSavingsOffset(WindowsTimezoneCache* cache);

  // Returns a string identifying the current timezone for the
  // timestamp taking into account daylight saving.
  char* LocalTimezone(WindowsTimezoneCache* cache);

 private:
  // Constants for time conversion.
  static const int64_t kTimeEpoc = 116444736000000000LL;
  static const int64_t kTimeScaler = 10000;
  static const int64_t kMsPerMinute = 60000;

  // Constants for timezone information.
  static const bool kShortTzNames = false;

  // Return whether or not daylight savings time is in effect at this time.
  bool InDST(WindowsTimezoneCache* cache);

  // Accessor for FILETIME representation.
  FILETIME& ft() { return time_.ft_; }

  // Accessor for integer representation.
  int64_t& t() { return time_.t_; }

  // Although win32 uses 64-bit integers for representing timestamps,
  // these are packed into a FILETIME structure. The FILETIME structure
  // is just a struct representing a 64-bit integer. The TimeStamp union
  // allows access to both a FILETIME and an integer representation of
  // the timestamp.
  union TimeStamp {
    FILETIME ft_;
    int64_t t_;
  };

  TimeStamp time_;
};


// Initialize timestamp to start of epoc.
Win32Time::Win32Time() {
  t() = 0;
}


// Initialize timestamp from a JavaScript timestamp.
Win32Time::Win32Time(double jstime) {
  t() = static_cast<int64_t>(jstime) * kTimeScaler + kTimeEpoc;
}


// Initialize timestamp from date/time components.
Win32Time::Win32Time(int year, int mon, int day, int hour, int min, int sec) {
  SYSTEMTIME st;
  st.wYear = year;
  st.wMonth = mon;
  st.wDay = day;
  st.wHour = hour;
  st.wMinute = min;
  st.wSecond = sec;
  st.wMilliseconds = 0;
  SystemTimeToFileTime(&st, &ft());
}


// Convert timestamp to JavaScript timestamp.
double Win32Time::ToJSTime() {
  return static_cast<double>((t() - kTimeEpoc) / kTimeScaler);
}


// Set timestamp to current time.
void Win32Time::SetToCurrentTime() {
  // The default GetSystemTimeAsFileTime has a ~15.5ms resolution.
  // Because we're fast, we like fast timers which have at least a
  // 1ms resolution.
  //
  // timeGetTime() provides 1ms granularity when combined with
  // timeBeginPeriod().  If the host application for v8 wants fast
  // timers, it can use timeBeginPeriod to increase the resolution.
  //
  // Using timeGetTime() has a drawback because it is a 32bit value
  // and hence rolls-over every ~49days.
  //
  // To use the clock, we use GetSystemTimeAsFileTime as our base;
  // and then use timeGetTime to extrapolate current time from the
  // start time.  To deal with rollovers, we resync the clock
  // any time when more than kMaxClockElapsedTime has passed or
  // whenever timeGetTime creates a rollover.

  static bool initialized = false;
  static TimeStamp init_time;
  static DWORD init_ticks;
  static const int64_t kHundredNanosecondsPerSecond = 10000000;
  static const int64_t kMaxClockElapsedTime =
      60*kHundredNanosecondsPerSecond;  // 1 minute

  // If we are uninitialized, we need to resync the clock.
  bool needs_resync = !initialized;

  // Get the current time.
  TimeStamp time_now;
  GetSystemTimeAsFileTime(&time_now.ft_);
  DWORD ticks_now = timeGetTime();

  // Check if we need to resync due to clock rollover.
  needs_resync |= ticks_now < init_ticks;

  // Check if we need to resync due to elapsed time.
  needs_resync |= (time_now.t_ - init_time.t_) > kMaxClockElapsedTime;

  // Check if we need to resync due to backwards time change.
  needs_resync |= time_now.t_ < init_time.t_;

  // Resync the clock if necessary.
  if (needs_resync) {
    GetSystemTimeAsFileTime(&init_time.ft_);
    init_ticks = ticks_now = timeGetTime();
    initialized = true;
  }

  // Finally, compute the actual time.  Why is this so hard.
  DWORD elapsed = ticks_now - init_ticks;
  this->time_.t_ = init_time.t_ + (static_cast<int64_t>(elapsed) * 10000);
}


// Return the local timezone offset in milliseconds east of UTC. This
// takes into account whether daylight saving is in effect at the time.
// Only times in the 32-bit Unix range may be passed to this function.
// Also, adding the time-zone offset to the input must not overflow.
// The function EquivalentTime() in date.js guarantees this.
int64_t Win32Time::LocalOffset(WindowsTimezoneCache* cache) {
  cache->InitializeIfNeeded();

  Win32Time rounded_to_second(*this);
  rounded_to_second.t() =
      rounded_to_second.t() / 1000 / kTimeScaler * 1000 * kTimeScaler;
  // Convert to local time using POSIX localtime function.
  // Windows XP Service Pack 3 made SystemTimeToTzSpecificLocalTime()
  // very slow.  Other browsers use localtime().

  // Convert from JavaScript milliseconds past 1/1/1970 0:00:00 to
  // POSIX seconds past 1/1/1970 0:00:00.
  double unchecked_posix_time = rounded_to_second.ToJSTime() / 1000;
  if (unchecked_posix_time > INT_MAX || unchecked_posix_time < 0) {
    return 0;
  }
  // Because _USE_32BIT_TIME_T is defined, time_t is a 32-bit int.
  time_t posix_time = static_cast<time_t>(unchecked_posix_time);

  // Convert to local time, as struct with fields for day, hour, year, etc.
  tm posix_local_time_struct;
  if (localtime_s(&posix_local_time_struct, &posix_time)) return 0;

  if (posix_local_time_struct.tm_isdst > 0) {
    return (cache->tzinfo_.Bias + cache->tzinfo_.DaylightBias) * -kMsPerMinute;
  } else if (posix_local_time_struct.tm_isdst == 0) {
    return (cache->tzinfo_.Bias + cache->tzinfo_.StandardBias) * -kMsPerMinute;
  } else {
    return cache->tzinfo_.Bias * -kMsPerMinute;
  }
}


// Return whether or not daylight savings time is in effect at this time.
bool Win32Time::InDST(WindowsTimezoneCache* cache) {
  cache->InitializeIfNeeded();

  // Determine if DST is in effect at the specified time.
  bool in_dst = false;
  if (cache->tzinfo_.StandardDate.wMonth != 0 ||
      cache->tzinfo_.DaylightDate.wMonth != 0) {
    // Get the local timezone offset for the timestamp in milliseconds.
    int64_t offset = LocalOffset(cache);

    // Compute the offset for DST. The bias parameters in the timezone info
    // are specified in minutes. These must be converted to milliseconds.
    int64_t dstofs =
        -(cache->tzinfo_.Bias + cache->tzinfo_.DaylightBias) * kMsPerMinute;

    // If the local time offset equals the timezone bias plus the daylight
    // bias then DST is in effect.
    in_dst = offset == dstofs;
  }

  return in_dst;
}


// Return the daylight savings time offset for this time.
int64_t Win32Time::DaylightSavingsOffset(WindowsTimezoneCache* cache) {
  return InDST(cache) ? 60 * kMsPerMinute : 0;
}


// Returns a string identifying the current timezone for the
// timestamp taking into account daylight saving.
char* Win32Time::LocalTimezone(WindowsTimezoneCache* cache) {
  // Return the standard or DST time zone name based on whether daylight
  // saving is in effect at the given time.
  return InDST(cache) ? cache->dst_tz_name_ : cache->std_tz_name_;
}


// Returns the accumulated user time for thread.
int OS::GetUserTime(uint32_t* secs,  uint32_t* usecs) {
  FILETIME dummy;
  uint64_t usertime;

  // Get the amount of time that the thread has executed in user mode.
  if (!GetThreadTimes(GetCurrentThread(), &dummy, &dummy, &dummy,
                      reinterpret_cast<FILETIME*>(&usertime))) return -1;

  // Adjust the resolution to micro-seconds.
  usertime /= 10;

  // Convert to seconds and microseconds
  *secs = static_cast<uint32_t>(usertime / 1000000);
  *usecs = static_cast<uint32_t>(usertime % 1000000);
  return 0;
}


// Returns current time as the number of milliseconds since
// 00:00:00 UTC, January 1, 1970.
double OS::TimeCurrentMillis() {
  return Time::Now().ToJsTime();
}

// Returns a string identifying the current timezone taking into
// account daylight saving.
const char* WindowsTimezoneCache::LocalTimezone(double time) {
  return Win32Time(time).LocalTimezone(this);
}

// Returns the local time offset in milliseconds east of UTC without
// taking daylight savings time into account.
double WindowsTimezoneCache::LocalTimeOffset(double time_ms, bool is_utc) {
  // Ignore is_utc and time_ms for now. That way, the behavior wouldn't
  // change with icu_timezone_data disabled.
  // Use current time, rounded to the millisecond.
  Win32Time t(OS::TimeCurrentMillis());
  // Time::LocalOffset inlcudes any daylight savings offset, so subtract it.
  return static_cast<double>(t.LocalOffset(this) -
                             t.DaylightSavingsOffset(this));
}

// Returns the daylight savings offset in milliseconds for the given
// time.
double WindowsTimezoneCache::DaylightSavingsOffset(double time) {
  int64_t offset = Win32Time(time).DaylightSavingsOffset(this);
  return static_cast<double>(offset);
}

TimezoneCache* OS::CreateTimezoneCache() { return new WindowsTimezoneCache(); }

int OS::GetLastError() {
  return ::GetLastError();
}


int OS::GetCurrentProcessId() {
  return static_cast<int>(::GetCurrentProcessId());
}


int OS::GetCurrentThreadId() {
  return static_cast<int>(::GetCurrentThreadId());
}

void OS::ExitProcess(int exit_code) {
  // Use TerminateProcess avoid races between isolate threads and
  // static destructors.
  fflush(stdout);
  fflush(stderr);
  TerminateProcess(GetCurrentProcess(), exit_code);
}

// ----------------------------------------------------------------------------
// Win32 console output.
//
// If a Win32 application is linked as a console application it has a normal
// standard output and standard error. In this case normal printf works fine
// for output. However, if the application is linked as a GUI application,
// the process doesn't have a console, and therefore (debugging) output is lost.
// This is the case if we are embedded in a windows program (like a browser).
// In order to be able to get debug output in this case the the debugging
// facility using OutputDebugString. This output goes to the active debugger
// for the process (if any). Else the output can be monitored using DBMON.EXE.

enum OutputMode {
  UNKNOWN,  // Output method has not yet been determined.
  CONSOLE,  // Output is written to stdout.
  ODS       // Output is written to debug facility.
};

static OutputMode output_mode = UNKNOWN;  // Current output mode.


// Determine if the process has a console for output.
static bool HasConsole() {
  // Only check the first time. Eventual race conditions are not a problem,
  // because all threads will eventually determine the same mode.
  if (output_mode == UNKNOWN) {
    // We cannot just check that the standard output is attached to a console
    // because this would fail if output is redirected to a file. Therefore we
    // say that a process does not have an output console if either the
    // standard output handle is invalid or its file type is unknown.
    if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE &&
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN)
      output_mode = CONSOLE;
    else
      output_mode = ODS;
  }
  return output_mode == CONSOLE;
}


static void VPrintHelper(FILE* stream, const char* format, va_list args) {
  if ((stream == stdout || stream == stderr) && !HasConsole()) {
    // It is important to use safe print here in order to avoid
    // overflowing the buffer. We might truncate the output, but this
    // does not crash.
    char buffer[4096];
    OS::VSNPrintF(buffer, sizeof(buffer), format, args);
    OutputDebugStringA(buffer);
  } else {
    vfprintf(stream, format, args);
  }
}


FILE* OS::FOpen(const char* path, const char* mode) {
  FILE* result;
  if (fopen_s(&result, path, mode) == 0) {
    return result;
  } else {
    return nullptr;
  }
}


bool OS::Remove(const char* path) {
  return (DeleteFileA(path) != 0);
}

char OS::DirectorySeparator() { return '\\'; }

bool OS::isDirectorySeparator(const char ch) {
  return ch == '/' || ch == '\\';
}


FILE* OS::OpenTemporaryFile() {
  // tmpfile_s tries to use the root dir, don't use it.
  char tempPathBuffer[MAX_PATH];
  DWORD path_result = 0;
  path_result = GetTempPathA(MAX_PATH, tempPathBuffer);
  if (path_result > MAX_PATH || path_result == 0) return nullptr;
  UINT name_result = 0;
  char tempNameBuffer[MAX_PATH];
  name_result = GetTempFileNameA(tempPathBuffer, "", 0, tempNameBuffer);
  if (name_result == 0) return nullptr;
  FILE* result = FOpen(tempNameBuffer, "w+");  // Same mode as tmpfile uses.
  if (result != nullptr) {
    Remove(tempNameBuffer);  // Delete on close.
  }
  return result;
}


// Open log file in binary mode to avoid /n -> /r/n conversion.
const char* const OS::LogFileOpenMode = "wb+";

// Print (debug) message to console.
void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}


void OS::VPrint(const char* format, va_list args) {
  VPrintHelper(stdout, format, args);
}


void OS::FPrint(FILE* out, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(out, format, args);
  va_end(args);
}


void OS::VFPrint(FILE* out, const char* format, va_list args) {
  VPrintHelper(out, format, args);
}


// Print error message to console.
void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
}


void OS::VPrintError(const char* format, va_list args) {
  VPrintHelper(stderr, format, args);
}


int OS::SNPrintF(char* str, int length, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = VSNPrintF(str, length, format, args);
  va_end(args);
  return result;
}


int OS::VSNPrintF(char* str, int length, const char* format, va_list args) {
  int n = _vsnprintf_s(str, length, _TRUNCATE, format, args);
  // Make sure to zero-terminate the string if the output was
  // truncated or if there was an error.
  if (n < 0 || n >= length) {
    if (length > 0)
      str[length - 1] = '\0';
    return -1;
  } else {
    return n;
  }
}


void OS::StrNCpy(char* dest, int length, const char* src, size_t n) {
  // Use _TRUNCATE or strncpy_s crashes (by design) if buffer is too small.
  size_t buffer_size = static_cast<size_t>(length);
  if (n + 1 > buffer_size)  // count for trailing '\0'
    n = _TRUNCATE;
  int result = strncpy_s(dest, length, src, n);
  USE(result);
  DCHECK(result == 0 || (n == _TRUNCATE && result == STRUNCATE));
}


#undef _TRUNCATE
#undef STRUNCATE

DEFINE_LAZY_LEAKY_OBJECT_GETTER(RandomNumberGenerator,
                                GetPlatformRandomNumberGenerator)
static LazyMutex rng_mutex = LAZY_MUTEX_INITIALIZER;

void OS::Initialize(bool hard_abort, const char* const gc_fake_mmap) {
  g_hard_abort = hard_abort;
}

// static
size_t OS::AllocatePageSize() {
  static size_t allocate_alignment = 0;
  if (allocate_alignment == 0) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    allocate_alignment = info.dwAllocationGranularity;
  }
  return allocate_alignment;
}

// static
size_t OS::CommitPageSize() {
  static size_t page_size = 0;
  if (page_size == 0) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    page_size = info.dwPageSize;
    DCHECK_EQ(4096, page_size);
  }
  return page_size;
}

// static
void OS::SetRandomMmapSeed(int64_t seed) {
  if (seed) {
    MutexGuard guard(rng_mutex.Pointer());
    GetPlatformRandomNumberGenerator()->SetSeed(seed);
  }
}

// static
void* OS::GetRandomMmapAddr() {
// The address range used to randomize RWX allocations in OS::Allocate
// Try not to map pages into the default range that windows loads DLLs
// Use a multiple of 64k to prevent committing unused memory.
// Note: This does not guarantee RWX regions will be within the
// range kAllocationRandomAddressMin to kAllocationRandomAddressMax
#ifdef V8_HOST_ARCH_64_BIT
  static const uintptr_t kAllocationRandomAddressMin = 0x0000000080000000;
  static const uintptr_t kAllocationRandomAddressMax = 0x000003FFFFFF0000;
#else
  static const uintptr_t kAllocationRandomAddressMin = 0x04000000;
  static const uintptr_t kAllocationRandomAddressMax = 0x3FFF0000;
#endif
  uintptr_t address;
  {
    MutexGuard guard(rng_mutex.Pointer());
    GetPlatformRandomNumberGenerator()->NextBytes(&address, sizeof(address));
  }
  address <<= kPageSizeBits;
  address += kAllocationRandomAddressMin;
  address &= kAllocationRandomAddressMax;
  return reinterpret_cast<void*>(address);
}

namespace {

DWORD GetProtectionFromMemoryPermission(OS::MemoryPermission access) {
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
    case OS::MemoryPermission::kNoAccessWillJitLater:
      return PAGE_NOACCESS;
    case OS::MemoryPermission::kRead:
      return PAGE_READONLY;
    case OS::MemoryPermission::kReadWrite:
      return PAGE_READWRITE;
    case OS::MemoryPermission::kReadWriteExecute:
      if (IsWindows10OrGreater())
        return PAGE_EXECUTE_READWRITE | PAGE_TARGETS_INVALID;
      return PAGE_EXECUTE_READWRITE;
    case OS::MemoryPermission::kReadExecute:
      if (IsWindows10OrGreater())
        return PAGE_EXECUTE_READ | PAGE_TARGETS_INVALID;
      return PAGE_EXECUTE_READ;
  }
  UNREACHABLE();
}

uint8_t* RandomizedVirtualAlloc(size_t size, DWORD flags, DWORD protect,
                                void* hint) {
  LPVOID base = nullptr;
  static BOOL use_aslr = -1;
#ifdef V8_HOST_ARCH_32_BIT
  // Don't bother randomizing on 32-bit hosts, because they lack the room and
  // don't have viable ASLR anyway.
  if (use_aslr == -1 && !IsWow64Process(GetCurrentProcess(), &use_aslr))
    use_aslr = FALSE;
#else
  use_aslr = TRUE;
#endif

  if (use_aslr && protect != PAGE_READWRITE) {
    // For executable or reserved pages try to randomize the allocation address.
    base = VirtualAlloc(hint, size, flags, protect);
  }

  // On failure, let the OS find an address to use.
  if (base == nullptr) {
    base = VirtualAlloc(nullptr, size, flags, protect);
  }
  return reinterpret_cast<uint8_t*>(base);
}

}  // namespace

// static
void* OS::Allocate(void* hint, size_t size, size_t alignment,
                   MemoryPermission access) {
  size_t page_size = AllocatePageSize();
  DCHECK_EQ(0, size % page_size);
  DCHECK_EQ(0, alignment % page_size);
  DCHECK_LE(page_size, alignment);
  hint = AlignedAddress(hint, alignment);

  DWORD flags = (access == OS::MemoryPermission::kNoAccess)
                    ? MEM_RESERVE
                    : MEM_RESERVE | MEM_COMMIT;
  DWORD protect = GetProtectionFromMemoryPermission(access);

  // First, try an exact size aligned allocation.
  uint8_t* base = RandomizedVirtualAlloc(size, flags, protect, hint);
  if (base == nullptr) return nullptr;  // Can't allocate, we're OOM.

  // If address is suitably aligned, we're done.
  uint8_t* aligned_base = reinterpret_cast<uint8_t*>(
      RoundUp(reinterpret_cast<uintptr_t>(base), alignment));
  if (base == aligned_base) return reinterpret_cast<void*>(base);

  // Otherwise, free it and try a larger allocation.
  CHECK(Free(base, size));

  // Clear the hint. It's unlikely we can allocate at this address.
  hint = nullptr;

  // Add the maximum misalignment so we are guaranteed an aligned base address
  // in the allocated region.
  size_t padded_size = size + (alignment - page_size);
  const int kMaxAttempts = 3;
  aligned_base = nullptr;
  for (int i = 0; i < kMaxAttempts; ++i) {
    base = RandomizedVirtualAlloc(padded_size, flags, protect, hint);
    if (base == nullptr) return nullptr;  // Can't allocate, we're OOM.

    // Try to trim the allocation by freeing the padded allocation and then
    // calling VirtualAlloc at the aligned base.
    CHECK(Free(base, padded_size));
    aligned_base = reinterpret_cast<uint8_t*>(
        RoundUp(reinterpret_cast<uintptr_t>(base), alignment));
    base = reinterpret_cast<uint8_t*>(
        VirtualAlloc(aligned_base, size, flags, protect));
    // We might not get the reduced allocation due to a race. In that case,
    // base will be nullptr.
    if (base != nullptr) break;
  }
  DCHECK_IMPLIES(base, base == aligned_base);
  return reinterpret_cast<void*>(base);
}

// static
bool OS::Free(void* address, const size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % AllocatePageSize());
  DCHECK_EQ(0, size % AllocatePageSize());
  USE(size);
  return VirtualFree(address, 0, MEM_RELEASE) != 0;
}

// static
bool OS::Release(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  return VirtualFree(address, size, MEM_DECOMMIT) != 0;
}

// static
bool OS::SetPermissions(void* address, size_t size, MemoryPermission access) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  if (access == MemoryPermission::kNoAccess) {
    return VirtualFree(address, size, MEM_DECOMMIT) != 0;
  }
  DWORD protect = GetProtectionFromMemoryPermission(access);
  return VirtualAlloc(address, size, MEM_COMMIT, protect) != nullptr;
}

// static
bool OS::DiscardSystemPages(void* address, size_t size) {
  // On Windows, discarded pages are not returned to the system immediately and
  // not guaranteed to be zeroed when returned to the application.
  using DiscardVirtualMemoryFunction =
      DWORD(WINAPI*)(PVOID virtualAddress, SIZE_T size);
  static std::atomic<DiscardVirtualMemoryFunction> discard_virtual_memory(
      reinterpret_cast<DiscardVirtualMemoryFunction>(-1));
  if (discard_virtual_memory ==
      reinterpret_cast<DiscardVirtualMemoryFunction>(-1))
    discard_virtual_memory =
        reinterpret_cast<DiscardVirtualMemoryFunction>(GetProcAddress(
            GetModuleHandle(L"Kernel32.dll"), "DiscardVirtualMemory"));
  // Use DiscardVirtualMemory when available because it releases faster than
  // MEM_RESET.
  DiscardVirtualMemoryFunction discard_function = discard_virtual_memory.load();
  if (discard_function) {
    DWORD ret = discard_function(address, size);
    if (!ret) return true;
  }
  // DiscardVirtualMemory is buggy in Win10 SP0, so fall back to MEM_RESET on
  // failure.
  void* ptr = VirtualAlloc(address, size, MEM_RESET, PAGE_READWRITE);
  CHECK(ptr);
  return ptr;
}

// static
bool OS::HasLazyCommits() {
  // TODO(alph): implement for the platform.
  return false;
}

void OS::Sleep(TimeDelta interval) {
  ::Sleep(static_cast<DWORD>(interval.InMilliseconds()));
}


void OS::Abort() {
  // Give a chance to debug the failure.
  if (IsDebuggerPresent()) {
    DebugBreak();
  }

  // Before aborting, make sure to flush output buffers.
  fflush(stdout);
  fflush(stderr);

  if (g_hard_abort) {
    V8_IMMEDIATE_CRASH();
  }
  // Make the MSVCRT do a silent abort.
  raise(SIGABRT);

  // Make sure function doesn't return.
  abort();
}


void OS::DebugBreak() {
#if V8_CC_MSVC
  // To avoid Visual Studio runtime support the following code can be used
  // instead
  // __asm { int 3 }
  __debugbreak();
#else
  ::DebugBreak();
#endif
}


class Win32MemoryMappedFile final : public OS::MemoryMappedFile {
 public:
  Win32MemoryMappedFile(HANDLE file, HANDLE file_mapping, void* memory,
                        size_t size)
      : file_(file),
        file_mapping_(file_mapping),
        memory_(memory),
        size_(size) {}
  ~Win32MemoryMappedFile() final;
  void* memory() const final { return memory_; }
  size_t size() const final { return size_; }

 private:
  HANDLE const file_;
  HANDLE const file_mapping_;
  void* const memory_;
  size_t const size_;
};


// static
OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name,
                                                 FileMode mode) {
  // Open a physical file.
  DWORD access = GENERIC_READ;
  if (mode == FileMode::kReadWrite) {
    access |= GENERIC_WRITE;
  }
  HANDLE file = CreateFileA(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return nullptr;

  DWORD size = GetFileSize(file, nullptr);
  if (size == 0) return new Win32MemoryMappedFile(file, nullptr, nullptr, 0);

  DWORD protection =
      (mode == FileMode::kReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
  // Create a file mapping for the physical file.
  HANDLE file_mapping =
      CreateFileMapping(file, nullptr, protection, 0, size, nullptr);
  if (file_mapping == nullptr) return nullptr;

  // Map a view of the file into memory.
  DWORD view_access =
      (mode == FileMode::kReadOnly) ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;
  void* memory = MapViewOfFile(file_mapping, view_access, 0, 0, size);
  return new Win32MemoryMappedFile(file, file_mapping, memory, size);
}

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name,
                                                   size_t size, void* initial) {
  // Open a physical file.
  HANDLE file = CreateFileA(name, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, 0, nullptr);
  if (file == nullptr) return nullptr;
  if (size == 0) return new Win32MemoryMappedFile(file, nullptr, nullptr, 0);
  // Create a file mapping for the physical file.
  HANDLE file_mapping = CreateFileMapping(file, nullptr, PAGE_READWRITE, 0,
                                          static_cast<DWORD>(size), nullptr);
  if (file_mapping == nullptr) return nullptr;
  // Map a view of the file into memory.
  void* memory = MapViewOfFile(file_mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (memory) memmove(memory, initial, size);
  return new Win32MemoryMappedFile(file, file_mapping, memory, size);
}


Win32MemoryMappedFile::~Win32MemoryMappedFile() {
  if (memory_) UnmapViewOfFile(memory_);
  if (file_mapping_) CloseHandle(file_mapping_);
  CloseHandle(file_);
}


// The following code loads functions defined in DbhHelp.h and TlHelp32.h
// dynamically. This is to avoid being depending on dbghelp.dll and
// tlhelp32.dll when running (the functions in tlhelp32.dll have been moved to
// kernel32.dll at some point so loading functions defines in TlHelp32.h
// dynamically might not be necessary any more - for some versions of Windows?).

// Function pointers to functions dynamically loaded from dbghelp.dll.
#define DBGHELP_FUNCTION_LIST(V)  \
  V(SymInitialize)                \
  V(SymGetOptions)                \
  V(SymSetOptions)                \
  V(SymGetSearchPath)             \
  V(SymLoadModule64)              \
  V(StackWalk64)                  \
  V(SymGetSymFromAddr64)          \
  V(SymGetLineFromAddr64)         \
  V(SymFunctionTableAccess64)     \
  V(SymGetModuleBase64)

// Function pointers to functions dynamically loaded from dbghelp.dll.
#define TLHELP32_FUNCTION_LIST(V)  \
  V(CreateToolhelp32Snapshot)      \
  V(Module32FirstW)                \
  V(Module32NextW)

// Define the decoration to use for the type and variable name used for
// dynamically loaded DLL function..
#define DLL_FUNC_TYPE(name) _##name##_
#define DLL_FUNC_VAR(name) _##name

// Define the type for each dynamically loaded DLL function. The function
// definitions are copied from DbgHelp.h and TlHelp32.h. The IN and VOID macros
// from the Windows include files are redefined here to have the function
// definitions to be as close to the ones in the original .h files as possible.
#ifndef IN
#define IN
#endif
#ifndef VOID
#define VOID void
#endif

// DbgHelp isn't supported on MinGW yet
#ifndef __MINGW32__
// DbgHelp.h functions.
using DLL_FUNC_TYPE(SymInitialize) = BOOL(__stdcall*)(IN HANDLE hProcess,
                                                      IN PSTR UserSearchPath,
                                                      IN BOOL fInvadeProcess);
using DLL_FUNC_TYPE(SymGetOptions) = DWORD(__stdcall*)(VOID);
using DLL_FUNC_TYPE(SymSetOptions) = DWORD(__stdcall*)(IN DWORD SymOptions);
using DLL_FUNC_TYPE(SymGetSearchPath) = BOOL(__stdcall*)(
    IN HANDLE hProcess, OUT PSTR SearchPath, IN DWORD SearchPathLength);
using DLL_FUNC_TYPE(SymLoadModule64) = DWORD64(__stdcall*)(
    IN HANDLE hProcess, IN HANDLE hFile, IN PSTR ImageName, IN PSTR ModuleName,
    IN DWORD64 BaseOfDll, IN DWORD SizeOfDll);
using DLL_FUNC_TYPE(StackWalk64) = BOOL(__stdcall*)(
    DWORD MachineType, HANDLE hProcess, HANDLE hThread,
    LPSTACKFRAME64 StackFrame, PVOID ContextRecord,
    PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
    PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
    PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
    PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
using DLL_FUNC_TYPE(SymGetSymFromAddr64) = BOOL(__stdcall*)(
    IN HANDLE hProcess, IN DWORD64 qwAddr, OUT PDWORD64 pdwDisplacement,
    OUT PIMAGEHLP_SYMBOL64 Symbol);
using DLL_FUNC_TYPE(SymGetLineFromAddr64) =
    BOOL(__stdcall*)(IN HANDLE hProcess, IN DWORD64 qwAddr,
                     OUT PDWORD pdwDisplacement, OUT PIMAGEHLP_LINE64 Line64);
// DbgHelp.h typedefs. Implementation found in dbghelp.dll.
using DLL_FUNC_TYPE(SymFunctionTableAccess64) = PVOID(__stdcall*)(
    HANDLE hProcess,
    DWORD64 AddrBase);  // DbgHelp.h typedef PFUNCTION_TABLE_ACCESS_ROUTINE64
using DLL_FUNC_TYPE(SymGetModuleBase64) = DWORD64(__stdcall*)(
    HANDLE hProcess,
    DWORD64 AddrBase);  // DbgHelp.h typedef PGET_MODULE_BASE_ROUTINE64

// TlHelp32.h functions.
using DLL_FUNC_TYPE(CreateToolhelp32Snapshot) =
    HANDLE(__stdcall*)(DWORD dwFlags, DWORD th32ProcessID);
using DLL_FUNC_TYPE(Module32FirstW) = BOOL(__stdcall*)(HANDLE hSnapshot,
                                                       LPMODULEENTRY32W lpme);
using DLL_FUNC_TYPE(Module32NextW) = BOOL(__stdcall*)(HANDLE hSnapshot,
                                                      LPMODULEENTRY32W lpme);

#undef IN
#undef VOID

// Declare a variable for each dynamically loaded DLL function.
#define DEF_DLL_FUNCTION(name) DLL_FUNC_TYPE(name) DLL_FUNC_VAR(name) = nullptr;
DBGHELP_FUNCTION_LIST(DEF_DLL_FUNCTION)
TLHELP32_FUNCTION_LIST(DEF_DLL_FUNCTION)
#undef DEF_DLL_FUNCTION

// Load the functions. This function has a lot of "ugly" macros in order to
// keep down code duplication.

static bool LoadDbgHelpAndTlHelp32() {
  static bool dbghelp_loaded = false;

  if (dbghelp_loaded) return true;

  HMODULE module;

  // Load functions from the dbghelp.dll module.
  module = LoadLibrary(TEXT("dbghelp.dll"));
  if (module == nullptr) {
    return false;
  }

#define LOAD_DLL_FUNC(name)                                                 \
  DLL_FUNC_VAR(name) =                                                      \
      reinterpret_cast<DLL_FUNC_TYPE(name)>(GetProcAddress(module, #name));

DBGHELP_FUNCTION_LIST(LOAD_DLL_FUNC)

#undef LOAD_DLL_FUNC

  // Load functions from the kernel32.dll module (the TlHelp32.h function used
  // to be in tlhelp32.dll but are now moved to kernel32.dll).
  module = LoadLibrary(TEXT("kernel32.dll"));
  if (module == nullptr) {
    return false;
  }

#define LOAD_DLL_FUNC(name)                                                 \
  DLL_FUNC_VAR(name) =                                                      \
      reinterpret_cast<DLL_FUNC_TYPE(name)>(GetProcAddress(module, #name));

TLHELP32_FUNCTION_LIST(LOAD_DLL_FUNC)

#undef LOAD_DLL_FUNC

  // Check that all functions where loaded.
bool result =
#define DLL_FUNC_LOADED(name) (DLL_FUNC_VAR(name) != nullptr)&&

    DBGHELP_FUNCTION_LIST(DLL_FUNC_LOADED)
        TLHELP32_FUNCTION_LIST(DLL_FUNC_LOADED)

#undef DLL_FUNC_LOADED
            true;

  dbghelp_loaded = result;
  return result;
  // NOTE: The modules are never unloaded and will stay around until the
  // application is closed.
}

#undef DBGHELP_FUNCTION_LIST
#undef TLHELP32_FUNCTION_LIST
#undef DLL_FUNC_VAR
#undef DLL_FUNC_TYPE


// Load the symbols for generating stack traces.
static std::vector<OS::SharedLibraryAddress> LoadSymbols(
    HANDLE process_handle) {
  static std::vector<OS::SharedLibraryAddress> result;

  static bool symbols_loaded = false;

  if (symbols_loaded) return result;

  BOOL ok;

  // Initialize the symbol engine.
  ok = _SymInitialize(process_handle,  // hProcess
                      nullptr,         // UserSearchPath
                      false);          // fInvadeProcess
  if (!ok) return result;

  DWORD options = _SymGetOptions();
  options |= SYMOPT_LOAD_LINES;
  options |= SYMOPT_FAIL_CRITICAL_ERRORS;
  options = _SymSetOptions(options);

  char buf[OS::kStackWalkMaxNameLen] = {0};
  ok = _SymGetSearchPath(process_handle, buf, OS::kStackWalkMaxNameLen);
  if (!ok) {
    int err = GetLastError();
    OS::Print("%d\n", err);
    return result;
  }

  HANDLE snapshot = _CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE,       // dwFlags
      GetCurrentProcessId());  // th32ProcessId
  if (snapshot == INVALID_HANDLE_VALUE) return result;
  MODULEENTRY32W module_entry;
  module_entry.dwSize = sizeof(module_entry);  // Set the size of the structure.
  BOOL cont = _Module32FirstW(snapshot, &module_entry);
  while (cont) {
    DWORD64 base;
    // NOTE the SymLoadModule64 function has the peculiarity of accepting a
    // both unicode and ASCII strings even though the parameter is PSTR.
    base = _SymLoadModule64(
        process_handle,                                       // hProcess
        0,                                                    // hFile
        reinterpret_cast<PSTR>(module_entry.szExePath),       // ImageName
        reinterpret_cast<PSTR>(module_entry.szModule),        // ModuleName
        reinterpret_cast<DWORD64>(module_entry.modBaseAddr),  // BaseOfDll
        module_entry.modBaseSize);                            // SizeOfDll
    if (base == 0) {
      int err = GetLastError();
      if (err != ERROR_MOD_NOT_FOUND &&
          err != ERROR_INVALID_HANDLE) {
        result.clear();
        return result;
      }
    }
    int lib_name_length = WideCharToMultiByte(
        CP_UTF8, 0, module_entry.szExePath, -1, nullptr, 0, nullptr, nullptr);
    std::string lib_name(lib_name_length, 0);
    WideCharToMultiByte(CP_UTF8, 0, module_entry.szExePath, -1, &lib_name[0],
                        lib_name_length, nullptr, nullptr);
    result.push_back(OS::SharedLibraryAddress(
        lib_name, reinterpret_cast<uintptr_t>(module_entry.modBaseAddr),
        reinterpret_cast<uintptr_t>(module_entry.modBaseAddr +
                                    module_entry.modBaseSize)));
    cont = _Module32NextW(snapshot, &module_entry);
  }
  CloseHandle(snapshot);

  symbols_loaded = true;
  return result;
}


std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  // SharedLibraryEvents are logged when loading symbol information.
  // Only the shared libraries loaded at the time of the call to
  // GetSharedLibraryAddresses are logged.  DLLs loaded after
  // initialization are not accounted for.
  if (!LoadDbgHelpAndTlHelp32()) return std::vector<OS::SharedLibraryAddress>();
  HANDLE process_handle = GetCurrentProcess();
  return LoadSymbols(process_handle);
}

void OS::SignalCodeMovingGC() {}

#else  // __MINGW32__
std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  return std::vector<OS::SharedLibraryAddress>();
}

void OS::SignalCodeMovingGC() {}
#endif  // __MINGW32__


int OS::ActivationFrameAlignment() {
#ifdef _WIN64
  return 16;  // Windows 64-bit ABI requires the stack to be 16-byte aligned.
#elif defined(__MINGW32__)
  // With gcc 4.4 the tree vectorization optimizer can generate code
  // that requires 16 byte alignment such as movdqa on x86.
  return 16;
#else
  return 8;  // Floating-point math runs faster with 8-byte alignment.
#endif
}

#if (defined(_WIN32) || defined(_WIN64))
void EnsureConsoleOutputWin32() {
  UINT new_flags =
      SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
  UINT existing_flags = SetErrorMode(new_flags);
  SetErrorMode(existing_flags | new_flags);
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _set_error_mode(_OUT_TO_STDERR);
#endif  // defined(_MSC_VER)
}
#endif  // (defined(_WIN32) || defined(_WIN64))

// ----------------------------------------------------------------------------
// Win32 thread support.

// Definition of invalid thread handle and id.
static const HANDLE kNoThread = INVALID_HANDLE_VALUE;

// Entry point for threads. The supplied argument is a pointer to the thread
// object. The entry function dispatches to the run method in the thread
// object. It is important that this function has __stdcall calling
// convention.
static unsigned int __stdcall ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  thread->NotifyStartedAndRun();
  return 0;
}


class Thread::PlatformData {
 public:
  explicit PlatformData(HANDLE thread) : thread_(thread) {}
  HANDLE thread_;
  unsigned thread_id_;
};


// Initialize a Win32 thread object. The thread has an invalid thread
// handle until it is started.

Thread::Thread(const Options& options)
    : stack_size_(options.stack_size()), start_semaphore_(nullptr) {
  data_ = new PlatformData(kNoThread);
  set_name(options.name());
}


void Thread::set_name(const char* name) {
  OS::StrNCpy(name_, sizeof(name_), name, strlen(name));
  name_[sizeof(name_) - 1] = '\0';
}


// Close our own handle for the thread.
Thread::~Thread() {
  if (data_->thread_ != kNoThread) CloseHandle(data_->thread_);
  delete data_;
}


// Create a new thread. It is important to use _beginthreadex() instead of
// the Win32 function CreateThread(), because the CreateThread() does not
// initialize thread specific structures in the C runtime library.
bool Thread::Start() {
  uintptr_t result = _beginthreadex(nullptr, static_cast<unsigned>(stack_size_),
                                    ThreadEntry, this, 0, &data_->thread_id_);
  data_->thread_ = reinterpret_cast<HANDLE>(result);
  return result != 0;
}

// Wait for thread to terminate.
void Thread::Join() {
  if (data_->thread_id_ != GetCurrentThreadId()) {
    WaitForSingleObject(data_->thread_, INFINITE);
  }
}


Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  DWORD result = TlsAlloc();
  DCHECK(result != TLS_OUT_OF_INDEXES);
  return static_cast<LocalStorageKey>(result);
}


void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  BOOL result = TlsFree(static_cast<DWORD>(key));
  USE(result);
  DCHECK(result);
}


void* Thread::GetThreadLocal(LocalStorageKey key) {
  return TlsGetValue(static_cast<DWORD>(key));
}


void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  BOOL result = TlsSetValue(static_cast<DWORD>(key), value);
  USE(result);
  DCHECK(result);
}

void OS::AdjustSchedulingParams() {}

// static
Stack::StackSlot Stack::GetStackStart() {
#if defined(V8_TARGET_ARCH_X64)
  return reinterpret_cast<void*>(
      reinterpret_cast<NT_TIB64*>(NtCurrentTeb())->StackBase);
#elif defined(V8_TARGET_ARCH_32_BIT)
  return reinterpret_cast<void*>(
      reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
#elif defined(V8_TARGET_ARCH_ARM64)
  // Windows 8 and later, see
  // https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadstacklimits
  ULONG_PTR lowLimit, highLimit;
  ::GetCurrentThreadStackLimits(&lowLimit, &highLimit);
  return reinterpret_cast<void*>(highLimit);
#else
#error Unsupported GetStackStart.
#endif
}

// static
Stack::StackSlot Stack::GetCurrentStackPosition() {
#if V8_CC_MSVC
  return _AddressOfReturnAddress();
#else
  return __builtin_frame_address(0);
#endif
}

}  // namespace base
}  // namespace v8
