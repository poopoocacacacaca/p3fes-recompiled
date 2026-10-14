// Derived from PCSX2 common/Linux/LnxMisc.cpp (GPL-3.0+); see the note below.
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "common/Pcsx2Types.h"
#include "common/Console.h"
#include "common/HostSys.h"
#include "common/Path.h"
#include "common/ScopedGuard.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/Threading.h"
#include "common/WindowInfo.h"

#include "fmt/format.h"

#include <spawn.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <optional>
#include <thread>

// Returns 0 on failure (not supported by the operating system).
u64 GetPhysicalMemory()
{
	u64 pages = 0;

#ifdef _SC_PHYS_PAGES
	pages = sysconf(_SC_PHYS_PAGES);
#endif

	return pages * getpagesize();
}

u64 GetAvailablePhysicalMemory()
{
	// Try to read MemAvailable from /proc/meminfo.
	FILE* file = fopen("/proc/meminfo", "r");
	if (file)
	{
		u64 mem_available = 0;
		u64 mem_free = 0, buffers = 0, cached = 0, sreclaimable = 0, shmem = 0;
		char line[256];

		while (fgets(line, sizeof(line), file))
		{
			// Modern kernels provide MemAvailable directly - preferred and most accurate.
			if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1)
			{
				fclose(file);
				return mem_available * _1kb;
			}

			// Fallback values for manual approximation.
			sscanf(line, "MemFree: %lu kB", &mem_free);
			sscanf(line, "Buffers: %lu kB", &buffers);
			sscanf(line, "Cached: %lu kB", &cached);
			sscanf(line, "SReclaimable: %lu kB", &sreclaimable);
			sscanf(line, "Shmem: %lu kB", &shmem);
		}
		fclose(file);

		// Fallback approximation: Linux-like heuristic.
		// available = MemFree + Buffers + Cached + SReclaimable - Shmem.
		const u64 available_kb = mem_free + buffers + cached + sreclaimable - shmem;
		return available_kb * _1kb;
	}

	// Fallback to sysinfo if /proc/meminfo couldn't be read.
	struct sysinfo info = {};
	if (sysinfo(&info) != 0)
		return 0;

	// Note: This does NOT include cached memory - only free + buffer.
	return (static_cast<u64>(info.freeram) + static_cast<u64>(info.bufferram)) * static_cast<u64>(info.mem_unit);
}

u64 GetTickFrequency()
{
	return 1000000000; // unix measures in nanoseconds
}

u64 GetCPUTicks()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (static_cast<u64>(ts.tv_sec) * 1000000000ULL) + ts.tv_nsec;
}

std::string GetOSVersionString()
{
#if defined(__linux__)
	FILE* file = fopen("/etc/os-release", "r");
	if (file)
	{
		char line[256];
		std::string distro;
		std::string version = "";
		while (fgets(line, sizeof(line), file))
		{
			std::string_view line_view(line);
			if (line_view.starts_with("NAME="))
			{
				distro = line_view.substr(5, line_view.size() - 6);
			}
			else if (line_view.starts_with("BUILD_ID="))
			{
				version = line_view.substr(9, line_view.size() - 10);
			}
			else if (line_view.starts_with("VERSION_ID="))
			{
				version = line_view.substr(11, line_view.size() - 12);
			}
		}
		fclose(file);

		// Some distros put quotes around the name and or version.
		if (distro.starts_with("\"") && distro.ends_with("\""))
			distro = distro.substr(1, distro.size() - 2);

		if (version.starts_with("\"") && version.ends_with("\""))
					version = version.substr(1, version.size() - 2);

		if (!distro.empty() && !version.empty())
			return fmt::format("{} {}", distro, version);
	}

	return "Linux";
#else // freebsd
	return "Other Unix";
#endif
}

// P3FES (pcsx2-gs): the GS library has no screensaver, mouse or sound duties; PCSX2's D-Bus and X11 versions of
// these are replaced with no-ops so the library needs neither.
bool Common::InhibitScreensaver(bool inhibit)
{
	return false;
}

void Common::SetMousePosition(int x, int y)
{
}

bool Common::AttachMousePositionCb(std::function<void(int, int)> cb)
{
	return false;
}

void Common::DetachMousePositionCb()
{
}

bool Common::PlaySoundAsync(const char* path)
{
	return false;
}


void Threading::Sleep(int ms)
{
	usleep(1000 * ms);
}

void Threading::SleepUntil(u64 ticks)
{
	struct timespec ts;
	ts.tv_sec = static_cast<time_t>(ticks / 1000000000ULL);
	ts.tv_nsec = static_cast<long>(ticks % 1000000000ULL);
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
}
