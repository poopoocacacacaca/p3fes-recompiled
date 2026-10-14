// SIO2 controller + PS2 memory card device for the ps2xIOP emulator.
//
// Register model, FIFO/queue behaviour and the memory card command protocol
// are adapted from PCSX2 (pcsx2/SIO/Sio2.cpp, pcsx2/SIO/Memcard/*),
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
//
// Card images use PCSX2's raw ".ps2" layout (528-byte pages: 512 data + 16
// spare/ECC), so a card file can be shared with PCSX2. Needs proper testing
// against more titles than the one it was written for.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ps2x::iop::detail
{
    class IopMemory;

    class IopMemoryCardImage
    {
    public:
        static constexpr uint32_t PageSize = 512u;
        static constexpr uint32_t SpareSize = 16u;
        static constexpr uint32_t RawPageSize = PageSize + SpareSize;
        static constexpr uint32_t PagesPerBlock = 16u;
        static constexpr uint32_t EraseSize = RawPageSize * PagesPerBlock;
        static constexpr uint32_t DefaultPages = 0x4000u; // 8 MB card

        bool open(const std::filesystem::path &path, bool createIfMissing, std::string &error);
        void close();

        [[nodiscard]] bool present() const noexcept { return !m_data.empty(); }
        [[nodiscard]] uint32_t pageCount() const noexcept;
        [[nodiscard]] const std::filesystem::path &path() const noexcept { return m_path; }

        void read(uint32_t address, uint8_t *destination, size_t size) const;
        // NAND semantics: a write can only clear bits; erase sets them back to 1.
        void write(uint32_t address, const uint8_t *source, size_t size);
        void eraseBlock(uint32_t address);

    private:
        void persist(uint32_t address, size_t size);

        std::filesystem::path m_path;
        std::vector<uint8_t> m_data;
        std::fstream m_file;
    };

    class IopSio2
    {
    public:
        static constexpr uint32_t RegisterBase = 0x1F808200u;
        static constexpr uint32_t RegisterEnd = 0x1F808284u;
        static constexpr uint32_t DataInAddress = 0x1F808260u;  // EE->device byte FIFO
        static constexpr uint32_t DataOutAddress = 0x1F808264u; // device->IOP byte FIFO
        static constexpr int Irq = 17;
        static constexpr int DmaInIrq = 0x2C;
        static constexpr int DmaOutIrq = 0x2D;

        using LogFn = std::function<void(std::string_view)>;

        IopSio2();

        void reset();
        // Attach card images. An empty path leaves that port without a card.
        void configureCards(const std::filesystem::path &port0, const std::filesystem::path &port1, LogFn log);

        [[nodiscard]] static bool isRegister(uint32_t phys) noexcept
        {
            return phys >= RegisterBase && phys < RegisterEnd;
        }

        uint32_t read32(uint32_t phys);
        // Returns true when the write requested an SIO2 transfer interrupt.
        bool write32(uint32_t phys, uint32_t value);

        void writeData(uint8_t value);
        uint8_t readData();

        // IOP DMA channel 11 (memory -> SIO2) and 12 (SIO2 -> memory).
        // Return the updated MADR.
        uint32_t dmaIn(IopMemory &memory, uint32_t madr, uint32_t bcr);
        uint32_t dmaOut(IopMemory &memory, uint32_t madr, uint32_t bcr);

        [[nodiscard]] bool cardPresent(int port) const noexcept;

    private:
        struct Card
        {
            IopMemoryCardImage image;
            uint8_t terminator = 0x55u;
            uint32_t sectorAddress = 0u;
            uint32_t transferAddress = 0u;
        };

        void softReset();
        void setCommand(size_t position, uint32_t value);

        void dispatchPad();
        void dispatchMultitap();
        void dispatchInfrared();
        void dispatchMemcard();

        uint8_t popIn();
        void twoBTerminator(Card &card, size_t length);

        void log(std::string_view text) const;

        std::array<uint32_t, 16> m_cmdQueue{};
        std::array<uint32_t, 4> m_portCtrl0{};
        std::array<uint32_t, 4> m_portCtrl1{};
        uint32_t m_ctrl = 0u;
        uint32_t m_cmdStat = 0u;
        uint32_t m_portStat = 0u;
        uint32_t m_fifoStat = 0u;
        uint32_t m_fifoTxPos = 0u;
        uint32_t m_fifoRxPos = 0u;
        uint32_t m_iStat = 0u;
        uint8_t m_port = 0u;

        bool m_queueRead = false;
        size_t m_queuePosition = 0u;
        size_t m_commandLength = 0u;
        size_t m_dmaBlockSize = 0u;
        bool m_queueComplete = false;

        std::deque<uint8_t> m_fifoIn;
        std::deque<uint8_t> m_fifoOut;

        std::array<Card, 2> m_cards;
        LogFn m_log;
        bool m_traceEnabled = false;
        int m_traceLevel = 0;
    };
}
