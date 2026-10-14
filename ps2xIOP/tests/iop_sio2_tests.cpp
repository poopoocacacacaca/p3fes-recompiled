// Round-trip test for the SIO2 memory card device: erase a block, write a page
// in 128-byte chunks, read it back, and check the image on disk.
#include "../src/emulator/core/iop_memory.h"
#include "../src/emulator/core/iop_sio2.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace ps2x::iop::detail;

namespace
{
    int g_failures = 0;
    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    // One PIO command: queue entry 0 = (rx << 18) | (tx << 8) | port, bytes, read back rx bytes.
    std::vector<uint8_t> command(IopMemory &mem, const std::vector<uint8_t> &tx, uint32_t rx)
    {
        mem.write32(0x1F808200u, (rx << 18u) | (static_cast<uint32_t>(tx.size()) << 8u) | 0x70u);
        mem.write32(0x1F808204u, 0u);
        for (uint8_t b : tx)
            mem.write8(0x1F808260u, b);
        mem.write32(0x1F808268u, 0x3BDu);
        mem.write32(0x1F808280u, 1u);
        std::vector<uint8_t> out(rx);
        for (uint32_t i = 0; i < rx; ++i)
            out[i] = mem.read8(0x1F808264u);
        return out;
    }

    std::vector<uint8_t> sectorCmd(uint8_t cmd, uint32_t page)
    {
        const uint8_t b0 = page & 0xFF, b1 = (page >> 8) & 0xFF, b2 = (page >> 16) & 0xFF, b3 = page >> 24;
        return {0x81, cmd, b0, b1, b2, b3, static_cast<uint8_t>(b0 ^ b1 ^ b2 ^ b3), 0, 0};
    }
}

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "ps2x_sio2_test" / "Mcd001.ps2";
    std::filesystem::remove_all(path.parent_path());

    IopMemory mem;
    mem.sio2().configureCards(path, {}, nullptr);
    check(mem.sio2().cardPresent(0), "card created");
    check(std::filesystem::file_size(path) == 0x840000u, "8MB+ECC image size");

    auto probe = command(mem, {0x81, 0x11, 0, 0}, 4);
    check(probe[2] == 0x2B && probe[3] == 0x55, "probe terminator");
    check(mem.read32(0x1F80826Cu) == 0x1100u, "cmdstat connected");

    auto specs = command(mem, {0x81, 0x26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 13);
    check(specs[3] == 0x00 && specs[4] == 0x02 && specs[5] == 0x10 && specs[8] == 0x40, "specs 512/16/0x4000");

    const uint32_t page = 0x123;
    // Erase the block containing the page, then write 528 bytes.
    command(mem, sectorCmd(0x21, page & ~15u), 9);
    command(mem, {0x81, 0x82, 0, 0}, 4);
    command(mem, sectorCmd(0x22, page), 9);
    std::vector<uint8_t> data(528);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i * 7 + 3);
    for (size_t off = 0; off < data.size(); off += 128)
    {
        const size_t n = std::min<size_t>(128, data.size() - off);
        std::vector<uint8_t> tx = {0x81, 0x42, static_cast<uint8_t>(n)};
        tx.insert(tx.end(), data.begin() + off, data.begin() + off + n);
        tx.push_back(0);
        tx.push_back(0);
        auto r = command(mem, tx, static_cast<uint32_t>(n + 6));
        uint8_t x = 0;
        for (size_t i = 0; i < n; ++i)
            x ^= data[off + i];
        check(r[n + 4] == x && r[n + 5] == 0x55, "write checksum/terminator");
    }
    command(mem, {0x81, 0x81, 0, 0}, 4);

    command(mem, sectorCmd(0x23, page), 9);
    std::vector<uint8_t> back;
    for (size_t off = 0; off < data.size(); off += 128)
    {
        const size_t n = std::min<size_t>(128, data.size() - off);
        std::vector<uint8_t> tx(3 + n + 2, 0);
        tx[0] = 0x81;
        tx[1] = 0x43;
        tx[2] = static_cast<uint8_t>(n);
        auto r = command(mem, tx, static_cast<uint32_t>(n + 6));
        back.insert(back.end(), r.begin() + 4, r.begin() + 4 + n);
    }
    check(back == data, "read back matches written page");

    std::ifstream file(path, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(page) * 528);
    std::vector<uint8_t> disk(528);
    file.read(reinterpret_cast<char *>(disk.data()), 528);
    check(disk == data, "image on disk matches");

    // DMA path: one 256-byte block in, one out (MC2_S1 style).
    std::vector<uint8_t> block(256, 0);
    block[0] = 0x81;
    block[1] = 0x11;
    for (size_t i = 0; i < block.size(); ++i)
        mem.write8(0x1000u + static_cast<uint32_t>(i), block[i]);
    mem.write32(0x1F808200u, (4u << 18u) | (2u << 8u) | 0x70u);
    mem.write32(0x1F808204u, 0u);
    mem.write32(0x1F801540u, 0x1000u);
    mem.write32(0x1F801544u, 0x00010040u);
    mem.write32(0x1F801548u, 0x01000201u);
    mem.write32(0x1F801550u, 0x2000u);
    mem.write32(0x1F801554u, 0x00010040u);
    mem.write32(0x1F801558u, 0x41000200u);
    check(mem.read8(0x2002u) == 0x2B && mem.read8(0x2003u) == 0x55, "DMA probe response");
    check((mem.read32(0x1F801548u) & 0x01000000u) == 0u, "DMA11 start bit cleared");

    std::printf(g_failures ? "%d failure(s)\n" : "sio2 tests passed\n", g_failures);
    std::filesystem::remove_all(path.parent_path());
    return g_failures ? 1 : 0;
}
