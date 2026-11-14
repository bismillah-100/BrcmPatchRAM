//
//  BlueToolFixup.cpp
//  BrcmPatchRAM
//
//  Created by Dhinak G on 6/11/21.
//


#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>
#include <Headers/kern_devinfo.hpp>

#include <mach-o/loader.h>
#include <mach/vm_map.h>
#include <sys/kauth.h>
#include <sys/vm.h>


#define MODULE_SHORT "btlfx"


class EXPORT BlueToolFixup : public IOService {
    OSDeclareDefaultStructors(BlueToolFixup)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(BlueToolFixup, IOService)


IOService *BlueToolFixup::probe(IOService *provider, SInt32 *score) {
    return ADDPR(startSuccess) ? IOService::probe(provider, score) : nullptr;
}

bool BlueToolFixup::start(IOService *provider) {
    if (!IOService::start(provider)) {
        SYSLOG("init", "failed to start the parent");
        return false;
    }
    setProperty("VersionInfo", kextVersion);
    registerService();
    
    return true;
}


#pragma mark - Patches

static const uint8_t kSkipUpdateFilePathOriginal[] = "/etc/bluetool/SkipBluetoothAutomaticFirmwareUpdate";
static const uint8_t kSkipUpdateFilePathPatched[]  = "/System/Library/CoreServices/boot.efi";


// Workaround 12.4 Beta 3+ bug where macOS may detect the Bluetooth chipset twice
// Once as internal, and second as an external dongle:
// 'ERROR -- Third Party Dongle has the same address as the internal module'
// Mainly applicable for BCM2046 and BCM2070 chipsets (BT2.1)
static const uint8_t kSkipAddressCheckOriginal[] =
{
    0x48, 0x89, 0xF3,             // mov    rbx, rsi
    0xE8, 0x00, 0x00, 0x00, 0x00, // call   <somewhere>
    0x85, 0xC0,                   // test   eax, eax
    0x74, 0x1D,                   // je
};

static const uint8_t kSkipAddressCheckPatched[] =
{
    0x48, 0x89, 0xF3,             // mov        rbx, rsi
    0xE8, 0x00, 0x00, 0x00, 0x00, // call       <somewhere>
    0x85, 0xC0,                   // test       eax, eax
    0x72, 0x1D,                   // jb short
};

static const uint8_t kSkipAddressCheckMask[] =
{
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
};

static const uint8_t kVendorCheckOriginal[] =
{
    0x81, 0xFA,              // cmp edx
    0x5C, 0x0A, 0x00, 0x00,  // Vendor BRCM,
    0x74                     // jnz short
};

static const uint8_t kVendorCheckPatched[] =
{
    0x81, 0xFA,              // cmp edx
    0x5C, 0x0A, 0x00, 0x00,  // Vendor BRCM,
    0xEB                     // jmp short
};

// Workaround for bugged chipset range check that
// doesn't consider 0 "THIRD_PARTY_DONGLE" as valid.
// This patch allows bluetooth to turn back on after the first power cycle.
// See https://github.com/acidanthera/BrcmPatchRAM/pull/18 for more details.
static const uint8_t kBadChipsetCheckOriginal[] =
{
    0x81, 0xF9,              // cmp ecx
    0xCF, 0x07, 0x00, 0x00,  // int 1999
    0x72                     // jb short
};

static const uint8_t kBadChipsetCheckPatched[] =
{
    0x81, 0xF9,              // cmp ecx
    0xCF, 0x07, 0x00, 0x00,  // int 1999
    0xEB                     // jmp short
};

static const uint8_t kBadChipsetCheckOriginal13_3[] =
{
    0x81, 0xF9,              // cmp ecx
    0x9E, 0x0F, 0x00, 0x00,  // int 3998
    0x77, 0x1A               // ja short
};

static const uint8_t kBadChipsetCheckPatched13_3[] =
{
    0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90
};

static const uint8_t kSkipInternalControllerNVRAMCheck13_3[] =
{
    0x41, 0x80, 0x00, 0x01, // xor     r15b, 1
    0x75, 0x00,             // jnz     short
    0x84, 0xDB,             // test    bl, bl
    0x75, 0x00              // jnz     short
};

static const uint8_t kSkipInternalControllerNVRAMCheckMask13_3[] =
{
    0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF,
    0xFF, 0x00
};

static const uint8_t kSkipInternalControllerNVRAMCheckPatched13_3[] =
{
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90,
    0x90, 0x90,
    0x90, 0x90
};

static const uint8_t kBadChipsetCheckOriginal15_4[] =
{
    0x81, 0xF9,              // cmp ecx
    0x6F, 0x17, 0x00, 0x00,  // int 5999
    0x77, 0x30               // ja short
};

static const uint8_t kBadChipsetCheckPatched15_4[] =
{
    0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90
};

static bool shouldPatchBoardId = false;
static bool shouldPatchAddress = false;
static bool shouldPatchNvramCheck = false;

static constexpr size_t kBoardIdSize = sizeof("Mac-F60DEB81FF30ACF6");
static constexpr size_t kBoardIdSizeLegacy = sizeof("Mac-F22586C8");

static const char boardIdsWithUSBBluetooth[][kBoardIdSize] = {
    "Mac-F60DEB81FF30ACF6",
    "Mac-9F18E312C5C2BF0B",
    "Mac-937CB26E2E02BB01",
    "Mac-E43C1C25D4880AD6",
    "Mac-06F11FD93F0323C5",
    "Mac-06F11F11946D27C5",
    "Mac-A369DDC4E67F1C45",
    "Mac-FFE5EF870D7BA81A",
    "Mac-DB15BD556843C820",
    "Mac-B809C3757DA9BB8D",
    "Mac-65CE76090165799A",
    "Mac-4B682C642B45593E",
    "Mac-77F17D7DA9285301",
    "Mac-BE088AF8C5EB4FA2"
};

enum class TargetKind : uint8_t {
    None = 0,
    BlueTool,
    Bluetoothd
};

struct PendingExec {
    TargetKind kind {TargetKind::None};
    task_t task {nullptr};
};

using t_taskSetMainThreadQos = void (*)(task_t, thread_t);
using t_getTaskMap = vm_map_t (*)(task_t);
using t_getMapMin = vm_map_offset_t (*)(vm_map_t);
using t_getBsdTaskInfo = proc_t (*)(task_t);
using t_vmMapReadUser = kern_return_t (*)(vm_map_t, vm_map_address_t, const void *, vm_size_t);
using t_vmMapWriteUser = kern_return_t (*)(vm_map_t, const void *, vm_map_address_t, vm_size_t);
using t_csprocDisableEnforcement = void (*)(proc_t);
using t_vmMapSwitchProtect = void (*)(vm_map_t, boolean_t);

struct CachedTargetPages {
    static constexpr size_t kMaxOffsets = 16;
    struct Entry {
        vm_map_offset_t rva;
        vm_prot_t initprot;
    };
    Entry entries[kMaxOffsets] {};
    size_t count {0};
    bool populated {false};
};

struct ScanRange {
    vm_map_offset_t start;
    vm_map_offset_t end;
    vm_prot_t initprot;
};

static mach_vm_address_t origTaskSetMainThreadQos {};
static mach_vm_address_t origTaskClearReturnWait {};
static t_getTaskMap orgGetTaskMap {nullptr};
static t_getMapMin orgGetMapMin {nullptr};
static t_getBsdTaskInfo orgGetBsdTaskInfo {nullptr};
static t_vmMapReadUser orgVmMapReadUser {nullptr};
static t_vmMapWriteUser orgVmMapWriteUser {nullptr};
static t_csprocDisableEnforcement orgCsprocDisableEnforcement {nullptr};
static t_vmMapSwitchProtect orgVmMapSwitchProtect {nullptr};

static kauth_listener_t execListenerHandle {nullptr};
static int execCookie {};
static ThreadLocal<PendingExec, 32> pendingExec;
static IOLock *cacheLock {nullptr};
static CachedTargetPages blueToolCache {};
static CachedTargetPages bluetoothdCache {};

#pragma mark - Process-private COW patching code

static inline bool replaceBytes(void *haystack, size_t haystackSize, const void *needle, size_t findSize, const void *patch, size_t replaceSize) {
    return KernelPatcher::findAndReplace(haystack, haystackSize, needle, findSize, patch, replaceSize);
}

template <size_t findSize, size_t replaceSize, typename T>
static inline bool replaceBytes(void *haystack, size_t haystackSize, const T (&needle)[findSize], const T (&patch)[replaceSize]) {
    return replaceBytes(haystack, haystackSize, needle, findSize * sizeof(T), patch, replaceSize * sizeof(T));
}

static inline bool replaceBytesWithMask(void *haystack, size_t haystackSize, const void *needle, size_t findSize, const void *findMask, size_t findMaskSize, const void *patch, size_t replaceSize, const void *patchMask, size_t replaceMaskSize) {
    return KernelPatcher::findAndReplaceWithMask(haystack, haystackSize, needle, findSize, findMask, findMaskSize, patch, replaceSize, patchMask, replaceMaskSize);
}

template <size_t findSize, size_t findMaskSize, size_t replaceSize, size_t replaceMaskSize, typename T>
static inline bool replaceBytesWithMask(void *haystack, size_t haystackSize, const T (&needle)[findSize], const T (&findMask)[findMaskSize], const T (&patch)[replaceSize], const T (&patchMask)[replaceMaskSize]) {
    return replaceBytesWithMask(haystack, haystackSize, needle, findSize * sizeof(T), findMask, findMaskSize * sizeof(T), patch, replaceSize * sizeof(T), patchMask, replaceSize * sizeof(T));
}

static const char *targetName(TargetKind kind) {
    return kind == TargetKind::BlueTool ? "BlueTool" : "bluetoothd";
}

static CachedTargetPages &cacheForTarget(TargetKind kind) {
    return kind == TargetKind::BlueTool ? blueToolCache : bluetoothdCache;
}

static bool applyPatchesToPage(TargetKind kind, uint8_t *pageBuf) {
    bool patched = false;
    if (kind == TargetKind::BlueTool) {
        patched |= replaceBytes(pageBuf, PAGE_SIZE, kSkipUpdateFilePathOriginal, kSkipUpdateFilePathPatched);
        if (shouldPatchBoardId)
            patched |= replaceBytes(pageBuf, PAGE_SIZE, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
    } else if (kind == TargetKind::Bluetoothd) {
        patched |= replaceBytes(pageBuf, PAGE_SIZE, kVendorCheckOriginal, kVendorCheckPatched);
        patched |= replaceBytes(pageBuf, PAGE_SIZE, kBadChipsetCheckOriginal, kBadChipsetCheckPatched);
        if (getKernelVersion() >= KernelVersion::Ventura && getKernelVersion() < KernelVersion::Sequoia)
            patched |= replaceBytes(pageBuf, PAGE_SIZE, kBadChipsetCheckOriginal13_3, kBadChipsetCheckPatched13_3);
        if (getKernelVersion() >= KernelVersion::Sequoia)
            patched |= replaceBytes(pageBuf, PAGE_SIZE, kBadChipsetCheckOriginal15_4, kBadChipsetCheckPatched15_4);
        if (shouldPatchNvramCheck)
            patched |= replaceBytesWithMask(pageBuf, PAGE_SIZE, kSkipInternalControllerNVRAMCheck13_3, sizeof(kSkipInternalControllerNVRAMCheck13_3), kSkipInternalControllerNVRAMCheckMask13_3, sizeof(kSkipInternalControllerNVRAMCheckMask13_3), kSkipInternalControllerNVRAMCheckPatched13_3, sizeof(kSkipInternalControllerNVRAMCheckPatched13_3), nullptr, 0);
        if (shouldPatchBoardId)
            patched |= replaceBytes(pageBuf, PAGE_SIZE, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
        if (shouldPatchAddress)
            patched |= replaceBytesWithMask(pageBuf, PAGE_SIZE, kSkipAddressCheckOriginal, kSkipAddressCheckMask, kSkipAddressCheckPatched, kSkipAddressCheckMask);
    }
    return patched;
}

static bool patchPageInMap(vm_map_t map, proc_t targetProc, bool &enforcementDisabled, vm_map_offset_t pageAddr, vm_prot_t initprot, const uint8_t *pageBuf) {
    if (!(initprot & VM_PROT_WRITE)) {
        if (!enforcementDisabled) {
            if (orgCsprocDisableEnforcement && targetProc)
                orgCsprocDisableEnforcement(targetProc);
            else if (orgVmMapSwitchProtect)
                orgVmMapSwitchProtect(map, FALSE);
            enforcementDisabled = true;
        }
        kern_return_t protErr = vm_protect(map, static_cast<vm_address_t>(pageAddr), PAGE_SIZE, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        if (protErr != KERN_SUCCESS) {
            SYSLOG(MODULE_SHORT, "vm_protect COW failed at 0x%llx (%d)", static_cast<uint64_t>(pageAddr), protErr);
            return false;
        }
    }

    kern_return_t writeErr = orgVmMapWriteUser(map, pageBuf, pageAddr, PAGE_SIZE);
    if (writeErr != KERN_SUCCESS) {
        SYSLOG(MODULE_SHORT, "vm_map_write_user failed at 0x%llx (%d)", static_cast<uint64_t>(pageAddr), writeErr);
        if (!(initprot & VM_PROT_WRITE))
            vm_protect(map, static_cast<vm_address_t>(pageAddr), PAGE_SIZE, FALSE, initprot);
        return false;
    }

    if (!(initprot & VM_PROT_WRITE)) {
        kern_return_t restoreErr = vm_protect(map, static_cast<vm_address_t>(pageAddr), PAGE_SIZE, FALSE, initprot);
        if (restoreErr != KERN_SUCCESS) {
            SYSLOG(MODULE_SHORT, "vm_protect restore failed at 0x%llx (%d)", static_cast<uint64_t>(pageAddr), restoreErr);
            return false;
        }
    }

    return true;
}

static bool patchFromCache(vm_map_t map, proc_t targetProc, bool &enforcementDisabled, vm_map_offset_t baseAddr, TargetKind kind, uint8_t *pageBuf) {
    CachedTargetPages snapshot {};
    IOLockLock(cacheLock);
    snapshot = cacheForTarget(kind);
    IOLockUnlock(cacheLock);

    if (!snapshot.populated || snapshot.count == 0)
        return false;

    for (size_t i = 0; i < snapshot.count; i++) {
        vm_map_offset_t pageAddr = baseAddr + snapshot.entries[i].rva;
        if (orgVmMapReadUser(map, pageAddr, pageBuf, PAGE_SIZE) != KERN_SUCCESS)
            return false;
        if (!applyPatchesToPage(kind, pageBuf))
            return false;
        if (!patchPageInMap(map, targetProc, enforcementDisabled, pageAddr, snapshot.entries[i].initprot, pageBuf))
            return false;
        DBGLOG(MODULE_SHORT, "applied cached COW patch to %s at rva 0x%llx", targetName(kind), static_cast<uint64_t>(snapshot.entries[i].rva));
    }

    return true;
}

static size_t collectScanRanges(vm_map_t map, vm_map_offset_t baseAddr, TargetKind kind, ScanRange *ranges, size_t maxRanges) {
    mach_header_64 header {};
    if (orgVmMapReadUser(map, baseAddr, &header, sizeof(header)) != KERN_SUCCESS || header.magic != MH_MAGIC_64) {
        SYSLOG(MODULE_SHORT, "failed to read Mach-O header for %s", targetName(kind));
        return 0;
    }

    static constexpr uint32_t kMaxCmdsSize = 64 * PAGE_SIZE;
    if (header.sizeofcmds == 0 || header.sizeofcmds > kMaxCmdsSize) {
        SYSLOG(MODULE_SHORT, "invalid sizeofcmds %u for %s", header.sizeofcmds, targetName(kind));
        return 0;
    }

    auto cmds = Buffer::create<uint8_t>(header.sizeofcmds);
    if (!cmds) {
        SYSLOG(MODULE_SHORT, "failed to allocate %u bytes for load commands", header.sizeofcmds);
        return 0;
    }

    size_t rangeCount = 0;
    if (orgVmMapReadUser(map, baseAddr + sizeof(header), cmds, header.sizeofcmds) == KERN_SUCCESS) {
        bool hasSlide = false;
        uint64_t slide = 0;
        const bool scanData = (kind == TargetKind::BlueTool && shouldPatchBoardId);

        uint32_t offset = 0;
        for (uint32_t i = 0; i < header.ncmds && offset + sizeof(load_command) <= header.sizeofcmds; i++) {
            const auto *lc = reinterpret_cast<const load_command *>(cmds + offset);
            if (lc->cmdsize < sizeof(load_command) || offset + lc->cmdsize > header.sizeofcmds)
                break;
            if (lc->cmd == LC_SEGMENT_64 && lc->cmdsize >= sizeof(segment_command_64)) {
                const auto *seg = reinterpret_cast<const segment_command_64 *>(lc);
                if (strncmp(seg->segname, SEG_TEXT, sizeof(seg->segname)) == 0) {
                    slide = static_cast<uint64_t>(baseAddr) - seg->vmaddr;
                    hasSlide = true;
                    break;
                }
            }
            offset += lc->cmdsize;
        }

        if (hasSlide) {
            offset = 0;
            for (uint32_t i = 0; i < header.ncmds && offset + sizeof(load_command) <= header.sizeofcmds && rangeCount < maxRanges; i++) {
                const auto *lc = reinterpret_cast<const load_command *>(cmds + offset);
                if (lc->cmdsize < sizeof(load_command) || offset + lc->cmdsize > header.sizeofcmds)
                    break;
                if (lc->cmd == LC_SEGMENT_64 && lc->cmdsize >= sizeof(segment_command_64)) {
                    const auto *seg = reinterpret_cast<const segment_command_64 *>(lc);
                    const bool isText = strncmp(seg->segname, SEG_TEXT, sizeof(seg->segname)) == 0;
                    const bool isData = scanData && strncmp(seg->segname, SEG_DATA, sizeof(seg->segname)) == 0;
                    if ((isText || isData) && seg->filesize > 0 && seg->vmsize > 0) {
                        uint64_t mappedBytes = (seg->filesize + PAGE_MASK) & ~static_cast<uint64_t>(PAGE_MASK);
                        if (mappedBytes > seg->vmsize)
                            mappedBytes = seg->vmsize & ~static_cast<uint64_t>(PAGE_MASK);
                        if (mappedBytes >= PAGE_SIZE) {
                            ranges[rangeCount].start = static_cast<vm_map_offset_t>(seg->vmaddr + slide);
                            ranges[rangeCount].end = ranges[rangeCount].start + static_cast<vm_map_offset_t>(mappedBytes);
                            ranges[rangeCount].initprot = seg->initprot;
                            rangeCount++;
                        }
                    }
                }
                offset += lc->cmdsize;
            }
        }
    }

    Buffer::deleter(cmds);
    return rangeCount;
}

static void patchTaskMap(vm_map_t map, proc_t targetProc, TargetKind kind) {
    if (!map || kind == TargetKind::None)
        return;

    vm_map_offset_t baseAddr = orgGetMapMin(map);
    auto pageBuf = Buffer::create<uint8_t>(PAGE_SIZE);
    if (!pageBuf) {
        SYSLOG(MODULE_SHORT, "failed to allocate page buffer for %s", targetName(kind));
        return;
    }

    bool enforcementDisabled = false;
    if (!patchFromCache(map, targetProc, enforcementDisabled, baseAddr, kind, pageBuf)) {
        ScanRange ranges[2] {};
        size_t rangeCount = collectScanRanges(map, baseAddr, kind, ranges, arrsize(ranges));

        CachedTargetPages discovered {};
        bool cacheOverflow = false;

        for (size_t r = 0; r < rangeCount; r++) {
            for (vm_map_offset_t addr = ranges[r].start; addr < ranges[r].end; addr += PAGE_SIZE) {
                if (orgVmMapReadUser(map, addr, pageBuf, PAGE_SIZE) != KERN_SUCCESS)
                    continue;
                if (!applyPatchesToPage(kind, pageBuf))
                    continue;

                vm_map_offset_t rva = addr - baseAddr;
                DBGLOG(MODULE_SHORT, "found patch target in %s at rva 0x%llx", targetName(kind), static_cast<uint64_t>(rva));
                if (patchPageInMap(map, targetProc, enforcementDisabled, addr, ranges[r].initprot, pageBuf)) {
                    if (discovered.count < CachedTargetPages::kMaxOffsets) {
                        discovered.entries[discovered.count++] = {rva, ranges[r].initprot};
                    } else {
                        cacheOverflow = true;
                    }
                }
            }
        }

        if (discovered.count > 0 && !cacheOverflow) {
            discovered.populated = true;
            IOLockLock(cacheLock);
            cacheForTarget(kind) = discovered;
            IOLockUnlock(cacheLock);
        }
    }

    Buffer::deleter(pageBuf);
}

#pragma mark - Patched functions

static int execListener(kauth_cred_t, void *idata, kauth_action_t action, uintptr_t, uintptr_t arg1, uintptr_t, uintptr_t) {
    if (idata != &execCookie || action != KAUTH_FILEOP_EXEC || !arg1)
        return 0;

    const char *path = reinterpret_cast<const char *>(arg1);
    static constexpr size_t dirLength = sizeof("/usr/sbin/") - 1;
    if (strncmp(path, "/usr/sbin/", dirLength) != 0) {
        pendingExec.erase();
        return 0;
    }

    TargetKind kind = TargetKind::None;
    if (strcmp(path + dirLength, "BlueTool") == 0)
        kind = TargetKind::BlueTool;
    else if (strcmp(path + dirLength, "bluetoothd") == 0)
        kind = TargetKind::Bluetoothd;

    if (kind != TargetKind::None) {
        if (!pendingExec.set(PendingExec{kind, nullptr}))
            SYSLOG(MODULE_SHORT, "failed to enqueue exec patch for %s", path);
    } else {
        pendingExec.erase();
    }

    return 0;
}

static void patched_task_set_main_thread_qos(task_t task, thread_t main_thread) {
    FunctionCast(patched_task_set_main_thread_qos, origTaskSetMainThreadQos)(task, main_thread);

    auto entry = pendingExec.get();
    if (!entry || entry->kind == TargetKind::None)
        return;

    proc_t targetProc = orgGetBsdTaskInfo(task);
    if (targetProc == current_proc()) {
        // Direct execve() path: proc_finalize_csflags already ran and _task_clear_return_wait will not run.
        TargetKind kind = entry->kind;
        pendingExec.erase();
        patchTaskMap(orgGetTaskMap(task), targetProc, kind);
    } else {
        // posix_spawn() path: defer until _task_clear_return_wait runs after proc_finalize_csflags.
        entry->task = task;
    }
}

static void patched_task_clear_return_wait(task_t task, uint32_t flags) {
    auto entry = pendingExec.get();
    if (entry && entry->kind != TargetKind::None && entry->task == task) {
        TargetKind kind = entry->kind;
        pendingExec.erase();
        patchTaskMap(orgGetTaskMap(task), orgGetBsdTaskInfo(task), kind);
    }

    FunctionCast(patched_task_clear_return_wait, origTaskClearReturnWait)(task, flags);
}


#pragma mark - Patches on start/stop

static void pluginStart() {
    SYSLOG(MODULE_SHORT, "start");
    if (getKernelVersion() < KernelVersion::Monterey)
        return;

    cacheLock = IOLockAlloc();
    if (!cacheLock) {
        SYSLOG(MODULE_SHORT, "failed to allocate cache lock");
        return;
    }

    lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
        if (getKernelVersion() >= KernelVersion::Sonoma) {
            shouldPatchBoardId = checkKernelArgument("-btlfxboardid");
        } else {
            auto boardId = BaseDeviceInfo::get().boardIdentifier;
            auto boardIdSize = strlen(boardId) + 1;
            shouldPatchBoardId = boardIdSize == kBoardIdSize || boardIdSize == kBoardIdSizeLegacy;
            if (shouldPatchBoardId)
                for (size_t i = 0; i < arrsize(boardIdsWithUSBBluetooth); i++)
                    if (strcmp(boardIdsWithUSBBluetooth[i], boardId) == 0) {
                        shouldPatchBoardId = false;
                        break;
                    }
        }
        if ((getKernelVersion() == KernelVersion::Monterey && getKernelMinorVersion() >= 5) || getKernelVersion() > KernelVersion::Monterey)
            // 12.4 Beta 3+, XNU 21.5
            shouldPatchAddress = checkKernelArgument("-btlfxallowanyaddr");
        if (getKernelVersion() <= KernelVersion::Sonoma)
            shouldPatchNvramCheck = checkKernelArgument("-btlfxnvramcheck");

        orgGetTaskMap = reinterpret_cast<t_getTaskMap>(patcher.solveSymbol(KernelPatcher::KernelID, "_get_task_map"));
        orgGetMapMin = reinterpret_cast<t_getMapMin>(patcher.solveSymbol(KernelPatcher::KernelID, "_get_map_min"));
        orgGetBsdTaskInfo = reinterpret_cast<t_getBsdTaskInfo>(patcher.solveSymbol(KernelPatcher::KernelID, "_get_bsdtask_info"));
        orgVmMapReadUser = reinterpret_cast<t_vmMapReadUser>(patcher.solveSymbol(KernelPatcher::KernelID, "_vm_map_read_user"));
        orgVmMapWriteUser = reinterpret_cast<t_vmMapWriteUser>(patcher.solveSymbol(KernelPatcher::KernelID, "_vm_map_write_user"));
        if (!orgGetTaskMap || !orgGetMapMin || !orgGetBsdTaskInfo || !orgVmMapReadUser || !orgVmMapWriteUser || patcher.getError() != KernelPatcher::Error::NoError) {
            SYSLOG(MODULE_SHORT, "failed to solve required vm_map symbols (%d)", static_cast<int>(patcher.getError()));
            patcher.clearError();
            return;
        }

        orgCsprocDisableEnforcement = reinterpret_cast<t_csprocDisableEnforcement>(patcher.solveSymbol(KernelPatcher::KernelID, "_csproc_disable_enforcement"));
        if (patcher.getError() != KernelPatcher::Error::NoError) {
            DBGLOG(MODULE_SHORT, "failed to resolve _csproc_disable_enforcement, trying _vm_map_switch_protect");
            patcher.clearError();
            orgCsprocDisableEnforcement = nullptr;
            orgVmMapSwitchProtect = reinterpret_cast<t_vmMapSwitchProtect>(patcher.solveSymbol(KernelPatcher::KernelID, "_vm_map_switch_protect"));
            if (patcher.getError() != KernelPatcher::Error::NoError) {
                DBGLOG(MODULE_SHORT, "failed to resolve _vm_map_switch_protect");
                patcher.clearError();
                orgVmMapSwitchProtect = nullptr;
            }
        }

        KernelPatcher::RouteRequest execRoutes[] {
            KernelPatcher::RouteRequest("_task_set_main_thread_qos", patched_task_set_main_thread_qos, origTaskSetMainThreadQos),
            KernelPatcher::RouteRequest("_task_clear_return_wait", patched_task_clear_return_wait, origTaskClearReturnWait)
        };
        if (!patcher.routeMultipleLong(KernelPatcher::KernelID, execRoutes, arrsize(execRoutes))) {
            SYSLOG(MODULE_SHORT, "failed to route task exec hooks");
            return;
        }

        pendingExec.init();
        execListenerHandle = kauth_listen_scope(KAUTH_SCOPE_FILEOP, execListener, &execCookie);
        if (!execListenerHandle)
            SYSLOG(MODULE_SHORT, "failed to register kauth exec listener");
    });
}

// Boot args.
static const char *bootargOff[] {
    "-btlfxoff"
};
static const char *bootargDebug[] {
    "-btlfxdbg"
};
static const char *bootargBeta[] {
    "-btlfxbeta"
};

// Plugin configuration.
PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Monterey,
    KernelVersion::Tahoe,
    pluginStart
};
