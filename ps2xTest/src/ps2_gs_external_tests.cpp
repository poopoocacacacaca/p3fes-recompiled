// GOW-Port: regresiones del observador independiente; el GS original sigue activo.
#include "MiniTest.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include <array>
#include <cstring>
#include <vector>

namespace {
struct Observer final : GSExternalObserver {
    std::vector<uint8_t> bytes;
    std::vector<unsigned> paths;
    unsigned direct=0, frames=0, resets=0;
    uint64_t tick=0, display=0, value=0;
    uint32_t field=0;
    void gif(GifPathId p,const uint8_t* b,uint32_t n) noexcept override {
        paths.push_back(static_cast<unsigned>(p)); bytes.insert(bytes.end(),b,b+n);
    }
    void reg(uint8_t, uint64_t v) noexcept override { ++direct; value=v; }
    void vsync(const GSRegisters& r,uint64_t t,uint32_t f) noexcept override {
        ++frames; tick=t;field=f;display=r.display1;
    }
    void reset() noexcept override { ++resets; }
};
}
void register_gs_external_tests() {
    MiniTest::Case("GS observador externo",[](TestCase& tc){
        tc.Run("fragmentos originales, PATH y registros sin duplicados",[](TestCase& t){
            PS2Memory memory;t.IsTrue(memory.initialize(),"memoria inicializada");
            GS gs;gs.init(memory.getGSVRAM(),PS2_GS_VRAM_SIZE,&memory.gs());
            auto observer=std::make_shared<Observer>();gs.setExternalObserver(observer);
            const std::array<uint64_t,4> packet{1|(1ull<<15)|(1ull<<60),0xe,0x1234,0x3b}; // TEXA.
            const auto* p=reinterpret_cast<const uint8_t*>(packet.data());
            gs.processGIFPacket(p,7,GifPathId::Path1);
            gs.processGIFPacket(p+7,25,GifPathId::Path1);
            gs.processGIFPacket(p,32,GifPathId::Path2);
            gs.processGIFPacket(p,32,GifPathId::Path3);
            t.Equals(observer->bytes.size(),size_t(96),"cada span aparece una vez");
            t.IsTrue(std::memcmp(observer->bytes.data(),p,32)==0,"tag fragmentado conservado");
            t.IsTrue(observer->paths==std::vector<unsigned>{1,1,2,3},"origen y orden conservados");
            t.Equals(observer->direct,0u,"registro decodificado no se entrega dos veces");
            gs.writeRegister(0x3b,0x42);
            t.Equals(observer->direct,1u,"escritura de stub cubierta");
            t.Equals(observer->value,0x42ull,"valor completo de escritura");
            gs.setExternalObserver({});gs.processGIFPacket(p,32,GifPathId::Path3);
            t.Equals(observer->bytes.size(),size_t(96),"desenlazar detiene entregas");
        });
        tc.Run("VSync explícito y reset conservan enlace",[](TestCase& t){
            PS2Memory memory;t.IsTrue(memory.initialize(),"memoria inicializada");
            GS gs;gs.init(memory.getGSVRAM(),PS2_GS_VRAM_SIZE,&memory.gs());
            auto observer=std::make_shared<Observer>();gs.setExternalObserver(observer);
            memory.gs().display1=0x1122334455667788ull;
            gs.notifyExternalVSync(17,1);gs.notifyExternalVSync(18,2);
            t.Equals(observer->frames,1u,"field inválido no se entrega");
            t.Equals(observer->tick,17ull,"tick explícito");
            t.Equals(observer->field,1u,"campo explícito");
            t.Equals(observer->display,0x1122334455667788ull,"registros completos");
            gs.reset();t.Equals(observer->resets,1u,"reset externo informado");
            t.IsTrue(gs.hasExternalObserver(),"reset conserva observador");
            gs.notifyExternalVSync(19,0);t.Equals(observer->frames,2u,"VSync tras reset");
        });
        tc.Run("atajo PACKED rechaza sin consumir y permite fallback",[](TestCase& t){
            PS2Memory memory;t.IsTrue(memory.initialize(),"memoria inicializada");
            GS gs;gs.init(memory.getGSVRAM(),PS2_GS_VRAM_SIZE,&memory.gs());
            auto observer=std::make_shared<Observer>();gs.setExternalObserver(observer);
            const std::array<uint64_t,4> packet{1|(1ull<<15)|(1ull<<60),0xe,0x1234,0x3b};
            const auto* p=reinterpret_cast<const uint8_t*>(packet.data());
            t.IsFalse(gs.processNativePackedGIFPacket(p,32,GifPathId::Path3),"atajo desactivado");
            t.Equals(observer->bytes.size(),size_t(0),"rechazo no consume bytes");
            gs.processGIFPacket(p,32,GifPathId::Path3);
            t.Equals(observer->bytes.size(),size_t(32),"fallback entrega tag original");
            gs.setExternalObserver({});
            t.IsTrue(gs.processNativePackedGIFPacket(p,32,GifPathId::Path3),"predeterminado conserva atajo");
        });
    });
}
