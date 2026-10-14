// GOW-Port: Count avanza sin esperas host y es compartido entre contextos EE.
#include "MiniTest.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <string>
#include <vector>

void register_cop0_count_tests()
{
    MiniTest::Case("GOW COP0 Count", [](TestCase &tc)
    {
        tc.Run("Count shares writes across contexts and wraps at 32 bits", [](TestCase &t)
        {
            PS2Runtime runtime;
            R5900Context first{}, second{};
            runtime.writeCop0Count(&first, 0xfffffffeu);
            t.Equals(runtime.readCop0Count(&first), 0xffffffffu, "Lectura consecutiva avanza");
            t.Equals(runtime.readCop0Count(&second), 0u, "Wrap y estado global en otro contexto");
            t.Equals(runtime.readCop0Count(&first), 1u, "Una copia antigua no rebobina Count");
            runtime.eeScheduler().accountCycles(19u);
            t.Equals(runtime.readCop0Count(&second), 20u, "Avanza por los ciclos EE contabilizados");
            runtime.writeCop0Count(&second, 50u);
            t.Equals(runtime.readCop0Count(&first), 51u, "MTC0 de otro contexto cambia la base");
            t.Equals(first.cop0_count, 51u, "Espejo del contexto que leyó el registro");
        });
        tc.Run("Count ends a bounded guest polling loop without host time", [](TestCase &t)
        {
            PS2Runtime runtime;
            R5900Context context{};
            runtime.writeCop0Count(&context, 0u);
            const uint32_t start = runtime.readCop0Count(&context);
            bool expired = false;
            for (uint32_t i = 0; i < 32; ++i)
            {
                runtime.eeScheduler().accountCycles(32u);
                if (uint32_t(runtime.readCop0Count(&context) - start) >= 512u)
                {
                    expired = true;
                    break;
                }
            }
            t.IsTrue(expired, "Un deadline EE termina aunque no haya avance de reloj host");
        });
        tc.Run("scheduler reset seeds Count and its cycle baseline together", [](TestCase &t)
        {
            PS2Runtime runtime;
            R5900Context seed{}, reader{};
            std::vector<uint8_t> ram(PS2_RAM_SIZE);
            runtime.eeScheduler().accountCycles(1000u);
            runtime.writeCop0Count(&reader, 9999u);
            seed.cop0_count = 123u;
            runtime.eeScheduler().reset(ram.data(), seed);
            t.Equals(runtime.readCop0Count(&reader), 124u, "Reset no hereda ni contador ni reloj anterior");
            runtime.eeScheduler().accountCycles(32u);
            t.Equals(runtime.readCop0Count(&reader), 156u, "La nueva ejecución contabiliza sus ciclos");
        });
        tc.Run("MFC0 and MTC0 Count use the runtime including the zero destination", [](TestCase &t)
        {
            ps2recomp::CodeGenerator generator({}, {});
            ps2recomp::Instruction instruction{};
            instruction.opcode = ps2recomp::OPCODE_COP0;
            instruction.rd = ps2recomp::COP0_REG_COUNT;
            instruction.rs = ps2recomp::COP0_MF;
            for (uint32_t rt : {0u, 5u})
            {
                instruction.rt = rt;
                const auto code = generator.translateInstruction(instruction);
                t.IsTrue(code.find("runtime->readCop0Count(ctx)") != std::string::npos,
                         "MFC0 actualiza Count incluso cuando se descarta el resultado");
                if (rt == 0u)
                    t.Equals(code, std::string("(void)runtime->readCop0Count(ctx);"),
                             "No ocultar el efecto dentro de SET_GPR_S32 que omite $zero");
                else
                    t.IsTrue(code.find("(int32_t)") != std::string::npos, "MFC0 conserva extensión de signo");
            }
            instruction.rs = ps2recomp::COP0_MT;
            instruction.rt = 7;
            t.IsTrue(generator.translateInstruction(instruction).find(
                         "runtime->writeCop0Count(ctx, GPR_U32(ctx, 7))") != std::string::npos,
                     "MTC0 escribe el valor global y su base de ciclos");
        });
    });
}
