// SIO2 controller + PS2 memory card device for the ps2xIOP emulator.
// Adapted from PCSX2 (pcsx2/SIO/Sio2.cpp, pcsx2/SIO/Memcard/MemoryCardProtocol.cpp,
// pcsx2/IopDma.cpp psxDma11/psxDma12, pcsx2/ps2/Iop/IopHw{Read,Write}.cpp).
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "iop_sio2.h"

#include "iop_memory.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace ps2x::iop::detail
{
    namespace
    {
        namespace SioMode
        {
            constexpr uint8_t Pad = 0x01u;
            constexpr uint8_t Multitap = 0x21u;
            constexpr uint8_t Infrared = 0x61u;
            constexpr uint8_t Memcard = 0x81u;
        }

        namespace McCmd
        {
            constexpr uint8_t Probe = 0x11u;
            constexpr uint8_t UnknownWriteDeleteEnd = 0x12u;
            constexpr uint8_t SetEraseSector = 0x21u;
            constexpr uint8_t SetWriteSector = 0x22u;
            constexpr uint8_t SetReadSector = 0x23u;
            constexpr uint8_t GetSpecs = 0x26u;
            constexpr uint8_t SetTerminator = 0x27u;
            constexpr uint8_t GetTerminator = 0x28u;
            constexpr uint8_t WriteData = 0x42u;
            constexpr uint8_t ReadData = 0x43u;
            constexpr uint8_t ReadWriteEnd = 0x81u;
            constexpr uint8_t EraseBlock = 0x82u;
            constexpr uint8_t UnknownBoot = 0xBFu;
            constexpr uint8_t AuthXor = 0xF0u;
            constexpr uint8_t AuthF3 = 0xF3u;
            constexpr uint8_t AuthF7 = 0xF7u;
        }

        namespace CmdStat
        {
            constexpr uint32_t Disconnected = 0x1D100u;
            constexpr uint32_t Connected = 0x1100u;
            constexpr uint32_t NoDevicesMissing = 0x1000u;
            constexpr uint32_t Port1Missing = 0x1D000u;
            constexpr uint32_t Port2Missing = 0x2D000u;
            constexpr uint32_t OnePortOpen = 0x100u;
            constexpr uint32_t TwoPortsOpen = 0x200u;
        }

        constexpr uint32_t kCtrlStartTransfer = 0x1u;
        constexpr uint32_t kCtrlSio2manReset = 0x3BCu;
        constexpr uint32_t kPortStatDefault = 0xFu;
        constexpr uint8_t kTerminatorReady = 0x55u;
        constexpr uint32_t kCommandLengthMask = 0x3FFu;

        bool envFlag(const char *name)
        {
            const char *value = std::getenv(name);
            return value && *value && *value != '0';
        }
    }

    // ---------------------------------------------------------------------
    // Card image
    // ---------------------------------------------------------------------

    bool IopMemoryCardImage::open(const std::filesystem::path &path, bool createIfMissing, std::string &error)
    {
        close();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            if (!createIfMissing)
            {
                error = "not found";
                return false;
            }
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path(), ec);
            // Same as PCSX2's "create new card": an unformatted (erased) 8 MB
            // card. The game/BIOS formats it on first use.
            std::ofstream create(path, std::ios::binary | std::ios::trunc);
            if (!create)
            {
                error = "could not create file";
                return false;
            }
            std::vector<uint8_t> erased(EraseSize, 0xFFu);
            for (uint32_t block = 0; block < DefaultPages / PagesPerBlock; ++block)
                create.write(reinterpret_cast<const char *>(erased.data()), static_cast<std::streamsize>(erased.size()));
            if (!create)
            {
                error = "could not write new card";
                return false;
            }
        }

        m_file.open(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!m_file)
        {
            error = "could not open for read/write";
            return false;
        }
        m_file.seekg(0, std::ios::end);
        const std::streamoff size = m_file.tellg();
        if (size <= 0 || (static_cast<uint64_t>(size) % RawPageSize) != 0u)
        {
            error = "size is not a multiple of 528 bytes (only raw .ps2 PS2 cards are supported)";
            m_file.close();
            return false;
        }
        m_data.resize(static_cast<size_t>(size));
        m_file.seekg(0, std::ios::beg);
        m_file.read(reinterpret_cast<char *>(m_data.data()), size);
        if (!m_file)
        {
            error = "read failed";
            m_data.clear();
            m_file.close();
            return false;
        }
        m_path = path;
        return true;
    }

    void IopMemoryCardImage::close()
    {
        if (m_file.is_open())
            m_file.close();
        m_data.clear();
        m_path.clear();
    }

    uint32_t IopMemoryCardImage::pageCount() const noexcept
    {
        return present() ? static_cast<uint32_t>(m_data.size() / RawPageSize) : DefaultPages;
    }

    void IopMemoryCardImage::read(uint32_t address, uint8_t *destination, size_t size) const
    {
        for (size_t i = 0; i < size; ++i)
        {
            const size_t offset = static_cast<size_t>(address) + i;
            destination[i] = offset < m_data.size() ? m_data[offset] : 0xFFu;
        }
    }

    void IopMemoryCardImage::write(uint32_t address, const uint8_t *source, size_t size)
    {
        if (address >= m_data.size())
            return;
        size = std::min<size_t>(size, m_data.size() - address);
        for (size_t i = 0; i < size; ++i)
            m_data[address + i] &= source[i];
        persist(address, size);
    }

    void IopMemoryCardImage::eraseBlock(uint32_t address)
    {
        if (address >= m_data.size())
            return;
        const size_t size = std::min<size_t>(EraseSize, m_data.size() - address);
        std::fill_n(m_data.begin() + address, size, uint8_t{0xFFu});
        persist(address, size);
    }

    void IopMemoryCardImage::persist(uint32_t address, size_t size)
    {
        if (!m_file.is_open())
            return;
        m_file.clear();
        m_file.seekp(static_cast<std::streamoff>(address), std::ios::beg);
        m_file.write(reinterpret_cast<const char *>(m_data.data() + address), static_cast<std::streamsize>(size));
        m_file.flush();
    }

    // ---------------------------------------------------------------------
    // SIO2
    // ---------------------------------------------------------------------

    IopSio2::IopSio2()
    {
        reset();
    }

    void IopSio2::reset()
    {
        softReset();
        m_cmdQueue.fill(0u);
        m_portCtrl0.fill(0u);
        m_portCtrl1.fill(0u);
        m_ctrl = kCtrlSio2manReset;
        m_cmdStat = CmdStat::Disconnected;
        m_portStat = kPortStatDefault;
        m_fifoStat = 0u;
        m_fifoTxPos = 0u;
        m_fifoRxPos = 0u;
        m_iStat = 0u;
        m_port = 0u;
        m_fifoOut.clear();
        for (Card &card : m_cards)
        {
            card.terminator = kTerminatorReady;
            card.sectorAddress = 0u;
            card.transferAddress = 0u;
        }
        m_traceEnabled = envFlag("PS2X_SIO2_LOG");
        if (const char *level = std::getenv("PS2X_SIO2_LOG"))
            m_traceLevel = std::atoi(level);
    }

    void IopSio2::configureCards(const std::filesystem::path &port0, const std::filesystem::path &port1, LogFn logFn)
    {
        m_log = std::move(logFn);
        const std::filesystem::path paths[2] = {port0, port1};
        for (int port = 0; port < 2; ++port)
        {
            m_cards[port].image.close();
            if (paths[port].empty())
                continue;
            std::string error;
            // Port 1 gets a fresh card when missing; port 2 only uses an existing file.
            if (m_cards[port].image.open(paths[port], port == 0, error))
            {
                log("[SIO2] memory card slot " + std::to_string(port + 1) + ": " + paths[port].string() +
                    " (" + std::to_string(m_cards[port].image.pageCount()) + " pages)");
            }
            else if (port == 0)
            {
                log("[SIO2] memory card slot 1 unavailable: " + paths[port].string() + ": " + error);
            }
        }
    }

    bool IopSio2::cardPresent(int port) const noexcept
    {
        return port >= 0 && port < 2 && m_cards[static_cast<size_t>(port)].image.present();
    }

    void IopSio2::log(std::string_view text) const
    {
        if (m_log)
            m_log(text);
    }

    void IopSio2::softReset()
    {
        m_queueRead = false;
        m_queuePosition = 0u;
        m_commandLength = 0u;
        m_dmaBlockSize = 0u;
        m_queueComplete = false;
        m_fifoIn.clear();
        m_cmdStat = 0u;
    }

    void IopSio2::setCommand(size_t position, uint32_t value)
    {
        m_cmdQueue[position] = value;
        if (position == 0u)
            softReset();
    }

    uint32_t IopSio2::read32(uint32_t phys)
    {
        const uint32_t offset = phys - 0x1F808000u;
        if (offset >= 0x200u && offset < 0x240u)
            return m_cmdQueue[(offset - 0x200u) / 4u];
        if (offset >= 0x240u && offset < 0x260u)
        {
            const uint32_t index = (offset - 0x240u) / 8u;
            return (offset & 4u) ? m_portCtrl1[index] : m_portCtrl0[index];
        }
        switch (offset)
        {
        case 0x268u:
            return m_ctrl;
        case 0x26Cu:
            return m_cmdStat;
        case 0x270u:
            return m_portStat;
        case 0x274u:
            return m_fifoStat;
        case 0x278u:
            return m_fifoTxPos;
        case 0x27Cu:
            return m_fifoRxPos;
        case 0x280u:
            return m_iStat;
        default:
            return 0u;
        }
    }

    bool IopSio2::write32(uint32_t phys, uint32_t value)
    {
        const uint32_t offset = phys - 0x1F808000u;
        if (offset >= 0x200u && offset < 0x240u)
        {
            setCommand((offset - 0x200u) / 4u, value);
            return false;
        }
        if (offset >= 0x240u && offset < 0x260u)
        {
            const uint32_t index = (offset - 0x240u) / 8u;
            if (offset & 4u)
                m_portCtrl1[index] = value;
            else
                m_portCtrl0[index] = value;
            return false;
        }
        switch (offset)
        {
        case 0x268u:
        {
            m_ctrl = value;
            if ((value & kCtrlStartTransfer) == 0u)
                return false;
            if (m_traceEnabled && m_traceLevel >= 3)
            {
                char text[96];
                std::snprintf(text, sizeof(text), "[SIO2] start transfer ctrl=%08X cmdstat=%08X out=%zu",
                              value, m_cmdStat, m_fifoOut.size());
                log(text);
            }
            // Transfers complete synchronously here, so the START bit reads back
            // clear like it does on hardware once the transfer finishes. Leaving it
            // set makes SIO2MAN's "ctrl |= 0xC" FIFO reset re-trigger a transfer and
            // a second IRQ, which double-signals its completion semaphore.
            m_ctrl = value & ~kCtrlStartTransfer;
            const bool raise = m_iStat == 0u;
            m_iStat |= 1u;
            return raise;
        }
        case 0x26Cu:
            m_cmdStat = value;
            return false;
        case 0x270u:
            m_portStat = value;
            return false;
        case 0x274u:
            m_fifoStat = value;
            return false;
        case 0x278u:
            m_fifoTxPos = value;
            return false;
        case 0x27Cu:
            m_fifoRxPos = value;
            return false;
        case 0x280u:
            m_iStat &= ~value;
            return false;
        default:
            return false;
        }
    }

    uint8_t IopSio2::popIn()
    {
        if (m_fifoIn.empty())
            return 0u;
        const uint8_t value = m_fifoIn.front();
        m_fifoIn.pop_front();
        return value;
    }

    void IopSio2::writeData(uint8_t data)
    {
        if (!m_queueRead)
        {
            if (m_queuePosition >= m_cmdQueue.size())
                return;

            const uint32_t currentCmd = m_cmdQueue[m_queuePosition];
            m_port = static_cast<uint8_t>(currentCmd & 1u);
            m_commandLength = (currentCmd >> 8u) & kCommandLengthMask;
            m_queueRead = true;
            if (m_commandLength == 0u)
                m_queueComplete = true;
            m_fifoIn.clear();
        }

        if (m_queueComplete)
            return;

        m_fifoIn.push_back(data);

        const bool commandDone = (m_fifoIn.size() == m_commandLength && m_dmaBlockSize == 0u) ||
                                 (m_dmaBlockSize != 0u && m_fifoIn.size() == m_dmaBlockSize);
        if (!commandDone)
            return;

        m_queueRead = false;
        ++m_queuePosition;

        const uint8_t sioMode = popIn();
        if (m_traceEnabled && m_traceLevel >= 2 && sioMode != SioMode::Memcard)
        {
            char text[96];
            std::snprintf(text, sizeof(text), "[SIO2] mode=%02X port=%u q=%08X", sioMode, m_port + 1u,
                          m_cmdQueue[m_queuePosition - 1u]);
            log(text);
        }
        switch (sioMode)
        {
        case SioMode::Pad:
            dispatchPad();
            break;
        case SioMode::Multitap:
            dispatchMultitap();
            break;
        case SioMode::Infrared:
            dispatchInfrared();
            break;
        case SioMode::Memcard:
            dispatchMemcard();
            break;
        default:
            m_fifoOut.push_back(0xFFu);
            m_cmdStat = CmdStat::Disconnected;
            break;
        }

        if (m_dmaBlockSize > 0u)
        {
            const size_t remainder = m_fifoOut.size() % m_dmaBlockSize;
            if (remainder != 0u)
                m_fifoOut.insert(m_fifoOut.end(), m_dmaBlockSize - remainder, uint8_t{0u});
        }
    }

    uint8_t IopSio2::readData()
    {
        if (m_fifoOut.empty())
            return 0xFFu;
        const uint8_t value = m_fifoOut.front();
        m_fifoOut.pop_front();
        return value;
    }

    uint32_t IopSio2::dmaIn(IopMemory &memory, uint32_t madr, uint32_t bcr)
    {
        const uint32_t blockBytes = (bcr & 0xFFFFu) * 4u;
        const uint32_t blocks = bcr >> 16u;
        m_dmaBlockSize = blockBytes;
        for (uint32_t block = 0; block < blocks; ++block)
        {
            for (uint32_t i = 0; i < blockBytes; ++i)
                writeData(memory.read8(madr++));
        }
        return madr;
    }

    uint32_t IopSio2::dmaOut(IopMemory &memory, uint32_t madr, uint32_t bcr)
    {
        const uint32_t size = (bcr >> 16u) * (bcr & 0xFFFFu) * 4u;
        for (uint32_t i = 0; i < size; ++i)
            memory.write8(madr++, readData());
        return madr;
    }

    void IopSio2::dispatchPad()
    {
        // Controllers are serviced by the runtime's libpad HLE on the EE side,
        // so the IOP sees an empty controller port (PCSX2 PadNotConnected).
        if (m_cmdStat & CmdStat::OnePortOpen)
        {
            m_cmdStat &= ~CmdStat::OnePortOpen;
            m_cmdStat |= CmdStat::TwoPortsOpen;
        }
        else
        {
            m_cmdStat |= CmdStat::OnePortOpen;
        }
        m_cmdStat |= CmdStat::NoDevicesMissing;
        m_cmdStat |= m_port == 0u ? CmdStat::Port1Missing : CmdStat::Port2Missing;

        m_fifoOut.push_back(0xFFu);
        while (!m_fifoIn.empty())
        {
            m_fifoIn.pop_front();
            m_fifoOut.push_back(0xFFu);
        }
    }

    void IopSio2::dispatchMultitap()
    {
        if (m_cmdStat & CmdStat::OnePortOpen)
        {
            m_cmdStat &= ~CmdStat::OnePortOpen;
            m_cmdStat |= CmdStat::TwoPortsOpen;
        }
        else
        {
            m_cmdStat |= CmdStat::OnePortOpen;
        }
        m_cmdStat |= CmdStat::NoDevicesMissing | CmdStat::Port1Missing;
        m_fifoOut.push_back(0xFFu);
        while (!m_fifoIn.empty())
        {
            m_fifoIn.pop_front();
            m_fifoOut.push_back(0xFFu);
        }
    }

    void IopSio2::dispatchInfrared()
    {
        m_cmdStat = CmdStat::Disconnected;
        m_fifoIn.clear();
        while (m_fifoOut.size() < m_commandLength)
            m_fifoOut.push_back(0xFFu);
    }

    void IopSio2::twoBTerminator(Card &card, size_t length)
    {
        while (m_fifoOut.size() + 2u < length)
            m_fifoOut.push_back(0x00u);
        m_fifoOut.push_back(0x2Bu);
        m_fifoOut.push_back(card.terminator);
    }

    void IopSio2::dispatchMemcard()
    {
        Card &card = m_cards[m_port & 1u];
        const bool present = card.image.present();
        m_cmdStat = present ? CmdStat::Connected : CmdStat::Disconnected;

        const std::vector<uint8_t> traceIn(m_fifoIn.begin(), m_fifoIn.end());
        const size_t traceOutStart = m_fifoOut.size();
        const uint8_t command = popIn();
        const uint8_t response = present ? 0x00u : 0xFFu;
        m_fifoOut.push_back(response);
        m_fifoOut.push_back(response);

        struct TraceOnExit
        {
            IopSio2 &self;
            const std::vector<uint8_t> &in;
            size_t outStart;
            uint8_t command;
            bool present;
            ~TraceOnExit()
            {
                if (!self.m_traceEnabled)
                    return;
                std::string text;
                char head[160];
                std::snprintf(head, sizeof(head), "[SIO2] mc port=%u cmd=%02X q=%08X ctrl0=%08X ctrl1=%08X present=%d",
                              self.m_port + 1u, command,
                              self.m_cmdQueue[self.m_queuePosition ? self.m_queuePosition - 1u : 0u],
                              self.m_portCtrl0[self.m_port & 3u], self.m_portCtrl1[self.m_port & 3u], present ? 1 : 0);
                text = head;
                if (self.m_traceLevel >= 2)
                {
                    text += " in:";
                    char byte[4];
                    for (size_t i = 0; i < in.size() && i < 24u; ++i)
                    {
                        std::snprintf(byte, sizeof(byte), " %02X", in[i]);
                        text += byte;
                    }
                    text += " out:";
                    for (size_t i = outStart; i < self.m_fifoOut.size() && i < outStart + 24u; ++i)
                    {
                        std::snprintf(byte, sizeof(byte), " %02X", self.m_fifoOut[i]);
                        text += byte;
                    }
                }
                self.log(text);
            }
        } traceOnExit{*this, traceIn, traceOutStart, command, present};

        if (!present)
        {
            // Dead air on an empty slot.
            while (!m_fifoIn.empty())
            {
                m_fifoIn.pop_front();
                m_fifoOut.push_back(0xFFu);
            }
            return;
        }

        switch (command)
        {
        case McCmd::Probe:
        case McCmd::UnknownWriteDeleteEnd:
        case McCmd::ReadWriteEnd:
            twoBTerminator(card, 4u);
            break;
        case McCmd::SetEraseSector:
        case McCmd::SetWriteSector:
        case McCmd::SetReadSector:
        {
            const uint8_t b0 = popIn();
            const uint8_t b1 = popIn();
            const uint8_t b2 = popIn();
            const uint8_t b3 = popIn();
            const uint8_t expected = popIn();
            if ((b0 ^ b1 ^ b2 ^ b3) != expected)
                log("[SIO2] memory card sector checksum mismatch");
            card.sectorAddress = static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8u) |
                                 (static_cast<uint32_t>(b2) << 16u) | (static_cast<uint32_t>(b3) << 24u);
            card.transferAddress = IopMemoryCardImage::RawPageSize * card.sectorAddress;
            twoBTerminator(card, 9u);
            break;
        }
        case McCmd::GetSpecs:
        {
            const uint16_t sectorSize = IopMemoryCardImage::PageSize;
            const uint16_t eraseBlock = IopMemoryCardImage::PagesPerBlock;
            const uint32_t sectors = card.image.pageCount();
            uint8_t xorValue = 0x12u; // 02 ^ 00 ^ 10 ^ 00
            m_fifoOut.push_back(0x2Bu);
            m_fifoOut.push_back(static_cast<uint8_t>(sectorSize & 0xFFu));
            m_fifoOut.push_back(static_cast<uint8_t>(sectorSize >> 8u));
            m_fifoOut.push_back(static_cast<uint8_t>(eraseBlock & 0xFFu));
            m_fifoOut.push_back(static_cast<uint8_t>(eraseBlock >> 8u));
            for (int i = 0; i < 4; ++i)
            {
                const uint8_t byte = static_cast<uint8_t>(sectors >> (i * 8));
                xorValue ^= byte;
                m_fifoOut.push_back(byte);
            }
            m_fifoOut.push_back(xorValue);
            m_fifoOut.push_back(card.terminator);
            break;
        }
        case McCmd::SetTerminator:
            card.terminator = popIn();
            m_fifoOut.push_back(0x00u);
            m_fifoOut.push_back(0x2Bu);
            m_fifoOut.push_back(card.terminator);
            break;
        case McCmd::GetTerminator:
            m_fifoOut.push_back(0x2Bu);
            m_fifoOut.push_back(card.terminator);
            m_fifoOut.push_back(card.terminator);
            break;
        case McCmd::WriteData:
        {
            m_fifoOut.push_back(0x00u);
            m_fifoOut.push_back(0x2Bu);
            const uint8_t length = popIn();
            std::vector<uint8_t> buffer;
            buffer.reserve(length);
            uint8_t checksum = 0u;
            for (uint32_t i = 0; i < length; ++i)
            {
                const uint8_t value = popIn();
                checksum ^= value;
                buffer.push_back(value);
                m_fifoOut.push_back(0x00u);
            }
            card.image.write(card.transferAddress, buffer.data(), buffer.size());
            m_fifoOut.push_back(checksum);
            m_fifoOut.push_back(card.terminator);
            card.transferAddress += length;
            break;
        }
        case McCmd::ReadData:
        {
            const uint8_t length = popIn();
            m_fifoOut.push_back(0x00u);
            m_fifoOut.push_back(0x2Bu);
            std::vector<uint8_t> buffer(length);
            card.image.read(card.transferAddress, buffer.data(), buffer.size());
            uint8_t checksum = 0u;
            for (const uint8_t value : buffer)
            {
                checksum ^= value;
                m_fifoOut.push_back(value);
            }
            m_fifoOut.push_back(checksum);
            m_fifoOut.push_back(card.terminator);
            card.transferAddress += length;
            break;
        }
        case McCmd::EraseBlock:
            card.image.eraseBlock(card.transferAddress);
            twoBTerminator(card, 4u);
            break;
        case McCmd::UnknownBoot:
        case McCmd::AuthF7:
            twoBTerminator(card, 5u);
            break;
        case McCmd::AuthF3:
            card.terminator = kTerminatorReady;
            twoBTerminator(card, 5u);
            break;
        case McCmd::AuthXor:
        {
            const uint8_t mode = popIn();
            switch (mode)
            {
            case 0x01u:
            case 0x02u:
            case 0x04u:
            case 0x0Fu:
            case 0x11u:
            case 0x13u:
            {
                m_fifoOut.push_back(0x00u);
                m_fifoOut.push_back(0x2Bu);
                uint8_t xorResult = 0u;
                for (int i = 0; i < 8; ++i)
                {
                    xorResult ^= popIn();
                    m_fifoOut.push_back(0x00u);
                }
                m_fifoOut.push_back(xorResult);
                m_fifoOut.push_back(card.terminator);
                break;
            }
            case 0x06u:
            case 0x07u:
            case 0x0Bu:
                twoBTerminator(card, 14u);
                break;
            default:
                twoBTerminator(card, 5u);
                break;
            }
            break;
        }
        default:
        {
            char text[64];
            std::snprintf(text, sizeof(text), "[SIO2] unhandled memory card command %02X", command);
            log(text);
            break;
        }
        }
    }
}
