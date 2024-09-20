#include <cstdint>
#include <tuple>
#include <vector>
#include <algorithm>
#include <fmt/core.h>
#include <algorithm>

class MmapTracker
{
public:
    MmapTracker() = default;

    void mmap(void* addr, size_t size, int prot, int flags);
    void munmap(void* addr, size_t size);
    void mprotect(void* addr, size_t len, int prot);

    void dump();

private:
    typedef std::vector<std::tuple<uintptr_t, uintptr_t, int, int>> Mmaps;

    bool intersects(Mmaps::const_iterator it, uintptr_t start, uintptr_t end);

private:
    Mmaps::iterator find(uintptr_t addr);
    Mmaps::const_iterator find(uintptr_t addr) const;

private:
    Mmaps mMmaps;
};

inline bool MmapTracker::intersects(Mmaps::const_iterator it, uintptr_t start, uintptr_t end)
{
    return it != mMmaps.end() && start < std::get<1>(*it) && std::get<0>(*it) < end;
}

MmapTracker::Mmaps::iterator MmapTracker::find(uintptr_t addr)
{
    auto it = std::upper_bound(mMmaps.begin(), mMmaps.end(), addr, [](auto address, const auto& item) {
        return address < std::get<0>(item);
    });
    if (it != mMmaps.begin())
        --it;
    while (it != mMmaps.end() && std::get<1>(*it) <= addr)
        ++it;
    return it;
}

MmapTracker::Mmaps::const_iterator MmapTracker::find(uintptr_t addr) const
{
    auto it = std::upper_bound(mMmaps.begin(), mMmaps.end(), addr, [](auto address, const auto& item) {
        return address < std::get<0>(item);
    });
    if (it != mMmaps.begin())
        --it;
    while (it != mMmaps.end() && std::get<1>(*it) <= addr)
        ++it;
    return it;
}

void MmapTracker::mmap(void* addr, size_t size, int prot, int flags)
{
    const auto iaddr = reinterpret_cast<uintptr_t>(addr);
    const auto iaddrend = iaddr + size;
    auto it = find(iaddr);
    if (intersects(it, iaddr, iaddrend)) {
        // got a hit
        do {
            if (std::get<2>(*it) != prot || std::get<3>(*it) != flags) {
                const auto curstart = std::get<0>(*it);
                const auto curend = std::get<1>(*it);
                const auto curprot = std::get<2>(*it);
                const auto curflags = std::get<3>(*it);

                if (curstart < iaddr) {
                    // item addr is prior to input addr, update end and add new item
                    std::get<1>(*it) = iaddr;
                    it = mMmaps.insert(it + 1, std::make_tuple(iaddr, std::min(curend, iaddrend), prot, flags));

                    // if we're fully contained in the item, we need to add one more at the end
                    if (iaddrend < curend) {
                        it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                    }
                    ++it;
                } else if (iaddr <= curstart && iaddrend >= curend) {
                    // item is fully contained in input addr, just update prot and flags
                    std::get<2>(*it) = prot;
                    std::get<3>(*it) = flags;
                    ++it;
                } else if (iaddrend < curend) {
                    // item end is past input end, update and add new item
                    std::get<1>(*it) = iaddrend;
                    std::get<2>(*it) = prot;
                    std::get<3>(*it) = flags;

                    it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                    return;
                }
            } else {
                ++it;
            }
        } while (intersects(it, iaddr, iaddrend));
    } else {
        mMmaps.insert(it != mMmaps.end() ? it + 1 : it, std::make_tuple(iaddr, iaddrend, prot, flags));
    }
}

void MmapTracker::munmap(void* addr, size_t size)
{
    const auto iaddr = reinterpret_cast<uintptr_t>(addr);
    const auto iaddrend = iaddr + size;
    auto it = find(iaddr);
    while (intersects(it, iaddr, iaddrend)) {
        const auto curstart = std::get<0>(*it);
        const auto curend = std::get<1>(*it);
        const auto curprot = std::get<2>(*it);
        const auto curflags = std::get<3>(*it);

        if (curstart < iaddr) {
            // item addr is prior to input addr, update end
            std::get<1>(*it) = iaddr;

            // if we're fully contained in the item, we need to add one more at the end
            if (iaddrend < curend) {
                it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
            }
            ++it;
        } else if (iaddr <= curstart && iaddrend >= curend) {
            // item is fully contained in input addr, remove item
            it = mMmaps.erase(it);
        } else if (iaddrend < curend) {
            // item end is past input end, update
            std::get<0>(*it) = iaddrend;
            return;
        }
    }
}

void MmapTracker::mprotect(void* addr, size_t size, int prot)
{
    const auto iaddr = reinterpret_cast<uintptr_t>(addr);
    const auto iaddrend = iaddr + size;
    auto it = find(iaddr);
    while (intersects(it, iaddr, iaddrend)) {
        if (std::get<2>(*it) != prot) {
            const auto curstart = std::get<0>(*it);
            const auto curend = std::get<1>(*it);
            const auto curprot = std::get<2>(*it);
            const auto curflags = std::get<3>(*it);

            if (curstart < iaddr) {
                // item addr is prior to input addr, update end and add new item
                std::get<1>(*it) = iaddr;
                it = mMmaps.insert(it + 1, std::make_tuple(iaddr, std::min(curend, iaddrend), prot, curflags));

                // if we're fully contained in the item, we need to add one more at the end
                if (iaddrend < curend) {
                    it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                }
                ++it;
            } else if (iaddr <= curstart && iaddrend >= curend) {
                // item is fully contained in input addr, just update prot and flags
                std::get<2>(*it) = prot;
                std::get<3>(*it) = curflags;
                ++it;
            } else if (iaddrend < curend) {
                // item end is past input end, update and add new item
                std::get<1>(*it) = iaddrend;
                std::get<2>(*it) = prot;
                std::get<3>(*it) = curflags;

                it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                return;
            }
        } else {
            ++it;
        }
    }
}

void MmapTracker::dump()
{
    for (const auto& t : mMmaps) {
        fmt::print("{:#x} {:#x}({:d}) {} {}\n", std::get<0>(t), std::get<1>(t), std::get<1>(t) - std::get<0>(t), std::get<2>(t), std::get<3>(t));
    }
    fmt::print("--\n");
}

static void* ptraddr(uintptr_t a)
{
    return reinterpret_cast<void*>(a);
}

int main(int, char**)
{
    MmapTracker tracker;

    tracker.mmap(ptraddr(0), 4096 * 100, 1, 1);
    tracker.mmap(ptraddr(0), 4096 * 100, 1, 1);
    tracker.dump();
    tracker.munmap(ptraddr(0 + 8192), 4096 * 10);
    tracker.dump();

    tracker.mmap(ptraddr(0), 4096 * 100, 2, 1);
    tracker.mmap(ptraddr(4096), 4096, 50, 1);
    tracker.dump();
    tracker.mmap(ptraddr((4096 * 100) + (4096 * 110)), 4096 * 100, 3, 1);
    tracker.dump();
    tracker.munmap(ptraddr((4096 * 101) + (4096 * 110)), 4096 * 99);
    tracker.dump();

    return 0;
}
