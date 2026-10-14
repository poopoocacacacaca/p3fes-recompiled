// GOW-Port: el filtro no gasta el cupo antes del tick y mantiene el límite antiguo.
#include "MiniTest.h"
#include "runtime/gs/gs_host_present_diag.h"
#include <cstdint>
#include <limits>

void runGsHostDiagWindowTests()
{
    MiniTest::Case("GOW GS host diagnostic window", [](TestCase &tc)
    {
        tc.Run("deferred acquisitions preserve the budget until the inclusive tick", [](TestCase &t)
        {
            uint32_t records = 0u;
            bool premature = false;
            for (uint64_t tick = 0u; tick < 6000u; ++tick)
                premature |= GSHostPresentDiag::consume(tick, 6000u, records);
            t.IsTrue(!premature, "No emitir registros anteriores al tick solicitado");
            t.Equals(records, 0u, "El arranque no consume el cupo de la partida");
            t.IsTrue(GSHostPresentDiag::consume(6000u, 6000u, records), "Inicio inclusivo");
            t.Equals(records, 1u, "La primera adquisición seleccionada consume uno");
        });
        tc.Run("default and deferred windows retain the per-instance 4096 limit", [](TestCase &t)
        {
            for (uint64_t start : {0ull, 6000ull})
            {
                uint32_t records = 0u;
                uint32_t accepted = 0u;
                for (uint64_t step = 0u; step < 5000u; ++step)
                    accepted += GSHostPresentDiag::consume(start + step, start, records) ? 1u : 0u;
                t.Equals(accepted, 4096u, "Mismo cupo para diagnóstico antiguo y diferido");
                t.Equals(records, 4096u, "No crecer tras agotar el cupo");
                t.IsTrue(!GSHostPresentDiag::consume(0u, start, records), "El retroceso no renueva el cupo");
            }
            uint32_t full = std::numeric_limits<uint32_t>::max();
            t.IsTrue(!GSHostPresentDiag::consume(6000u, 6000u, full), "No desbordar el contador");
        });
        tc.Run("from-tick accepts decimal uint64 and rejects signs suffixes and overflow", [](TestCase &t)
        {
            t.Equals(GSHostPresentDiag::parseFromTick(nullptr), uint64_t(0u), "Ausente conserva el diagnóstico antiguo");
            t.Equals(GSHostPresentDiag::parseFromTick("6000"), uint64_t(6000u), "Tick solicitado");
            t.Equals(GSHostPresentDiag::parseFromTick("18446744073709551615"),
                     std::numeric_limits<uint64_t>::max(), "Último tick válido");
            for (const char *invalid : {"", "-1", "+1", "1x", " 1", "18446744073709551616"})
                t.Equals(GSHostPresentDiag::parseFromTick(invalid), uint64_t(0u), "Inválido conserva inicio cero");
        });
    });
}
