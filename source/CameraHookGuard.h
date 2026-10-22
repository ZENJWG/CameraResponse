#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstddef>

namespace cr {

// Never dereference or patch a fixed EXE address unless it belongs to an
// accessible executable image page. VirtualQuery does not read that page.
inline bool ReadableImageCode(uintptr_t address, std::size_t bytes) {
    if (!address || !bytes || bytes > 256 || address + bytes < address)
        return false;
    const uintptr_t processImage =
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!processImage)
        return false;

    const uintptr_t end = address + bytes;
    for (uintptr_t at = address; at < end;) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)))
            return false;
        if (info.State != MEM_COMMIT || info.Type != MEM_IMAGE ||
            reinterpret_cast<uintptr_t>(info.AllocationBase) != processImage ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;

        const DWORD access = info.Protect & 0xff;
        if (access != PAGE_EXECUTE_READ &&
            access != PAGE_EXECUTE_READWRITE &&
            access != PAGE_EXECUTE_WRITECOPY)
            return false;
        const uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (regionEnd <= at)
            return false;
        at = regionEnd;
    }
    return true;
}

// GTA SA classic PC US 1.0 is a 32-bit PE based at 0x400000.
// The callsite signature is validated separately; this prevents trying to
// read those addresses in a re-based, wrong-architecture or altered EXE.
inline bool ClassicSaImageLayout() {
    const uintptr_t base =
        reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (base != 0x400000u)
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew < 64 || dos->e_lfanew > 1024)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        base + static_cast<uintptr_t>(dos->e_lfanew));
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           nt->OptionalHeader.ImageBase == 0x400000u &&
           nt->OptionalHeader.SizeOfImage > 0x330000u;
}
} // namespace cr
