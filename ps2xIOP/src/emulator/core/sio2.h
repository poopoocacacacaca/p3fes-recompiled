#pragma once

// Emulation of the IOP's SIO2 (0x1F808200-0x1F8082FF) and the PS2 memory card.
// sio2man writes the command queue (SEND3), the bytes of each command (FIFO or DMA 11), sets CTRL bit 0 and
// waits for interrupt 17; then it reads RECV1-3 and the reply (FIFO or DMA 12). Each command starts with the
// mode (0x01 pad, 0x21 multitap, 0x61 infrared, 0x81 memory card). Pads and multitap read as disconnected
// (libpad is HLE on the EE). The memory card keeps raw pages (512 + 16 ECC) in a PCSX2-format file (8 MB).
// References: PCSX2 (pcsx2/SIO/Sio2.cpp, Memcard/MemoryCardProtocol.cpp) and ps2sdk (sio2man, mcman).

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace ps2x::iop::detail
{
    class MemoryCard
    {
    public:
        static constexpr uint32_t PageSize = 512u;
        static constexpr uint32_t SpareSize = 16u;
        static constexpr uint32_t RawPageSize = PageSize + SpareSize; // 528
        static constexpr uint32_t PagesPerBlock = 16u;
        static constexpr uint32_t PageCount = 0x4000u; // 8 MB
        static constexpr uint32_t ImageSize = RawPageSize * PageCount;

        // Empty path: the card lives in memory only (tests). If the file does not exist the card starts
        // unformatted (0xFF) and the file is created on the first write.
        void insert(std::string path);
        void eject();
        [[nodiscard]] bool present() const noexcept { return m_present; }
        [[nodiscard]] const std::string &path() const noexcept { return m_path; }

        void setAddress(uint32_t page) noexcept { m_address = page * RawPageSize; }
        void read(uint8_t *destination, size_t size);
        void write(const uint8_t *source, size_t size); // flash only turns bits from 1 to 0 (AND)
        void eraseBlock();                              // 16 pages from the current address
        [[nodiscard]] uint32_t address() const noexcept { return m_address; }
        [[nodiscard]] const std::vector<uint8_t> &image();

        uint8_t terminator = 0x55u;

    private:
        void load();
        void persist(uint32_t offset, size_t size);

        std::string m_path;
        bool m_present = false;
        bool m_loaded = false;
        bool m_fileExists = false;
        std::vector<uint8_t> m_image;
        uint32_t m_address = 0;
    };

    class Sio2
    {
    public:
        static constexpr uint32_t RegBase = 0x1F808200u;
        static constexpr uint32_t RegEnd = 0x1F808300u;
        static constexpr uint32_t RegFifoIn = 0x1F808260u;
        static constexpr uint32_t RegFifoOut = 0x1F808264u;
        static constexpr uint32_t RegCtrl = 0x1F808268u;
        static constexpr uint32_t RegRecv1 = 0x1F80826Cu;
        static constexpr uint32_t RegRecv2 = 0x1F808270u;
        static constexpr uint32_t RegRecv3 = 0x1F808274u;
        static constexpr uint32_t RegIstat = 0x1F808280u;
        static constexpr int IopInterrupt = 17;

        // RECV1: 0x1000 = a device answered; 0xD000 with the port's bit = device missing.
        static constexpr uint32_t StatConnected = 0x1100u;
        static constexpr uint32_t StatDisconnected = 0x1D100u;

        Sio2();
        void reset();

        [[nodiscard]] static bool contains(uint32_t physical) noexcept
        {
            return physical >= RegBase && physical < RegEnd;
        }
        [[nodiscard]] uint32_t read32(uint32_t physical);
        void write32(uint32_t physical, uint32_t value);
        [[nodiscard]] uint8_t readFifo();
        void writeFifo(uint8_t value);

        // DMA 11 (SIO2 in, bloques de blockBytes) y DMA 12 (SIO2 out)
        void dmaIn(const uint8_t *source, size_t blockBytes, size_t blocks);
        void dmaOut(uint8_t *destination, size_t bytes);

        // true once per CTRL write with bit 0 (transfer start): interrupt 17 must be raised.
        [[nodiscard]] bool takeTransferStarted() noexcept;

        [[nodiscard]] MemoryCard &card(uint32_t port) { return m_cards[port & 1u]; }
        // Looked up the first time each port is accessed, so the disk is not touched if the game never uses the card.
        void setCardPathProvider(std::function<std::string(uint32_t port)> provider);

        // Diagnostico (PS2X_SIO2_DIAG): ultimo comando de memory card procesado
        [[nodiscard]] uint8_t lastCardCommand() const noexcept { return m_lastCardCommand; }

    private:
        void softReset();
        [[nodiscard]] uint8_t popIn();
        void process();
        void absentDevice(bool memcard);
        void memcard();
        void prepareCard(uint32_t port);

        std::array<uint32_t, 16> m_cmdQueue{};
        std::array<uint32_t, 8> m_portCtrl{};
        uint32_t m_ctrl = 0x3BCu;
        uint32_t m_cmdStat = 0;
        uint32_t m_portStat = 0xFu;
        uint32_t m_fifoStat = 0;
        uint32_t m_unknown78 = 0, m_unknown7C = 0;
        uint32_t m_istat = 0;

        std::deque<uint8_t> m_in;
        std::deque<uint8_t> m_out;
        bool m_queueRead = false;
        size_t m_queuePosition = 0;
        size_t m_commandLength = 0;
        size_t m_dmaBlockSize = 0;
        bool m_queueComplete = false;
        uint32_t m_port = 0;
        bool m_transferStarted = false;

        std::array<MemoryCard, 2> m_cards{};
        std::array<bool, 2> m_cardPrepared{};
        std::function<std::string(uint32_t)> m_pathProvider;
        uint8_t m_lastCardCommand = 0;
        bool m_diag = false;
    };
}
