#pragma once

// SPU2 emulation (two cores of 24 voices, 2 MB of sound RAM): registers, RAM, manual and DMA transfers,
// ADPCM voices with pitch, ADSR, fixed volume and dry mix, readable ENDX/ENVX/NAX and the IRQA interrupt.
// The mix goes to the host's audio output (ps2_runtime.cpp).
// AutoDMA (ADMA) sound-data input, used by CRI ADX for streamed music and movie audio.
// Not yet: reverb, volume sweeps and Gaussian interpolation.
// References: PCSX2 (pcsx2/SPU2) and psx-spx's SPU notes (ADPCM and ADSR are the same as on the PS1).

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ps2x::iop::detail
{
    class Spu2
    {
    public:
        static constexpr uint32_t RegBase = 0x1F900000u;
        static constexpr uint32_t RegEnd = 0x1F900800u;
        static constexpr uint32_t RamHalfwords = 1u << 20u; // 2 MB
        static constexpr uint32_t VoicesPerCore = 24u;
        static constexpr uint32_t CyclesPerSample = 768u;  // 36.864 MHz / 48 kHz
        static constexpr int IopInterrupt = 9;

        // Offsets within a core (core 1 is 0x400 higher)
        static constexpr uint32_t RegPmon = 0x180u, RegNon = 0x184u, RegVmixL = 0x188u, RegVmixEL = 0x18Cu;
        static constexpr uint32_t RegVmixR = 0x190u, RegVmixER = 0x194u, RegMmix = 0x198u, RegAttr = 0x19Au;
        static constexpr uint32_t RegIrqa = 0x19Cu, RegKon = 0x1A0u, RegKoff = 0x1A4u, RegTsa = 0x1A8u;
        static constexpr uint32_t RegData = 0x1ACu, RegAdmas = 0x1B0u, RegVoiceAddr = 0x1C0u;
        static constexpr uint32_t RegEndx = 0x340u, RegStatx = 0x344u;
        static constexpr uint32_t RegMvolBase = 0x760u, RegMvolStride = 0x28u;
        static constexpr uint16_t StatxDmaReady = 0x0080u;

        Spu2();
        void reset();

        // Acceso a registros (direccion fisica del IOP, 16 bits)
        [[nodiscard]] static bool contains(uint32_t physical) noexcept
        {
            return physical >= RegBase && physical < RegEnd;
        }
        [[nodiscard]] uint16_t read16(uint32_t physical) const;
        void write16(uint32_t physical, uint16_t value);

        // RAM de sonido
        [[nodiscard]] uint16_t ram(uint32_t halfwordAddress) const noexcept
        {
            return m_ram[halfwordAddress & (RamHalfwords - 1u)];
        }
        void writeRam(uint32_t halfwordAddress, uint16_t value) noexcept;

        // IOP DMA (channels 4 and 7). Copies bytes to/from IOP RAM at the core's TSA and advances it.
        // In ADMA mode (ADMAS bit of the core set) a write queues the bytes as sound-data input instead:
        // 512-byte blocks of 256 left samples followed by 256 right samples, consumed at 48 kHz.
        bool dmaWrite(uint32_t core, const uint8_t *source, size_t bytes);
        bool dmaRead(uint32_t core, uint8_t *destination, size_t bytes);
        void markDmaReady(uint32_t core) noexcept;
        // ADMA: the core streams sound-data input (its ADMAS bit is set).
        [[nodiscard]] bool admaActive(uint32_t core) const noexcept { return (reg(core * 0x400u + RegAdmas) & (core + 1u)) != 0u; }
        // ADMA transfers complete when their last block enters the core's two-block input buffer (like the
        // hardware, which requests the next block while the other one plays); true once per completed transfer.
        [[nodiscard]] bool takeAdmaDone(uint32_t core) noexcept;

        // Advances to the given IOP cycle (one sample every CyclesPerSample cycles).
        void advanceTo(uint64_t iopCycle);
        void step(); // one 48 kHz sample
        [[nodiscard]] bool takeInterrupt() noexcept;

        // Interleaved stereo samples (L, R) produced since the last call. Producer (IOP) and consumer (host audio
        // thread) can be different threads. With maxLatencyFrames the oldest are dropped when more delay has built
        // up (emulation can run faster than real time).
        [[nodiscard]] size_t pendingFrames() const;
        size_t drainOutput(int16_t *stereo, size_t maxFrames, size_t maxLatencyFrames = SIZE_MAX);

        // A voice's state, for tests and diagnostics
        struct VoiceState
        {
            bool active = false;
            uint32_t nax = 0;          // siguiente bloque ADPCM (medias palabras)
            uint32_t lsa = 0;          // inicio del bucle
            bool lsaFromRegister = false;
            uint32_t pitchCounter = 0; // 4.12
            int16_t decoded[28]{};
            uint32_t decodedIndex = 28; // 28 = the next block must be decoded
            int32_t hist1 = 0, hist2 = 0;
            int16_t prev[3]{};
            uint8_t blockFlags = 0;
            enum class Phase : uint8_t { Off, Attack, Decay, Sustain, Release } phase = Phase::Off;
            int32_t envelope = 0;
            uint32_t envelopeCounter = 0;
            int16_t lastOutput = 0;
        };
        [[nodiscard]] const VoiceState &voice(uint32_t core, uint32_t index) const { return m_voices[core][index]; }

    private:
        [[nodiscard]] uint16_t &reg(uint32_t offset) noexcept { return m_regs[(offset & 0x7FFu) >> 1u]; }
        [[nodiscard]] uint16_t reg(uint32_t offset) const noexcept { return m_regs[(offset & 0x7FFu) >> 1u]; }
        [[nodiscard]] uint32_t reg32(uint32_t offset) const noexcept; // par alto/bajo (direccion de 20 bits)
        void setReg32(uint32_t offset, uint32_t value) noexcept;
        [[nodiscard]] uint32_t voiceReg(uint32_t core, uint32_t voice, uint32_t field) const noexcept
        {
            return core * 0x400u + voice * 0x10u + field;
        }
        [[nodiscard]] uint32_t voiceAddrReg(uint32_t core, uint32_t voice, uint32_t field) const noexcept
        {
            return core * 0x400u + RegVoiceAddr + voice * 0xCu + field;
        }

        void keyOn(uint32_t core, uint32_t mask, uint32_t firstVoice);
        void keyOff(uint32_t core, uint32_t mask, uint32_t firstVoice);
        void decodeBlock(uint32_t core, uint32_t voice);
        int32_t stepVoice(uint32_t core, uint32_t voice);
        void stepEnvelope(uint32_t core, uint32_t voice);
        void checkIrqAddress(uint32_t halfwordAddress) noexcept;

        void feedInput(uint32_t core);

        // ADMA sound-data input per core: queued PCM bytes and a ring of decoded stereo frames.
        static constexpr uint32_t InputFrames = 512u;
        struct InputState
        {
            std::vector<uint8_t> queue;
            size_t queueRead = 0;
            std::array<std::array<int16_t, 2>, InputFrames> ring{};
            uint32_t ringRead = 0;
            uint32_t ringCount = 0;
            uint64_t bytesQueued = 0;   // total bytes ever queued
            uint64_t bytesConsumed = 0; // total bytes ever moved into the ring
            std::vector<uint64_t> transferEnds; // bytesQueued at the end of each pending DMA transfer
            uint32_t doneCount = 0;
        };
        std::array<InputState, 2> m_input{};

        std::vector<uint16_t> m_ram;
        std::array<uint16_t, 0x400> m_regs{};
        std::array<std::array<VoiceState, VoicesPerCore>, 2> m_voices{};
        std::array<uint32_t, 2> m_endx{};
        std::vector<int16_t> m_output;
        mutable std::mutex m_outputMutex;
        uint64_t m_lastCycle = 0;
        bool m_interrupt = false;
    };
}
