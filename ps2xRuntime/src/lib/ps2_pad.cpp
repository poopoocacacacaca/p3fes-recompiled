#include "runtime/ps2_pad.h"
#include "ps2_host_backend.h"
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr uint8_t kPadAnalogMarker = 0x73;
    constexpr uint8_t kPadStickCenter = 0x80;

    constexpr uint16_t PAD_LEFT = 0x0080u;
    constexpr uint16_t PAD_DOWN = 0x0040u;
    constexpr uint16_t PAD_RIGHT = 0x0020u;
    constexpr uint16_t PAD_UP = 0x0010u;
    constexpr uint16_t PAD_START = 0x0008u;
    constexpr uint16_t PAD_R3 = 0x0004u;
    constexpr uint16_t PAD_L3 = 0x0002u;
    constexpr uint16_t PAD_SELECT = 0x0001u;
    constexpr uint16_t PAD_SQUARE = 0x8000u;
    constexpr uint16_t PAD_CROSS = 0x4000u;
    constexpr uint16_t PAD_CIRCLE = 0x2000u;
    constexpr uint16_t PAD_TRIANGLE = 0x1000u;
    constexpr uint16_t PAD_R1 = 0x0800u;
    constexpr uint16_t PAD_L1 = 0x0400u;
    constexpr uint16_t PAD_R2 = 0x0200u;
    constexpr uint16_t PAD_L2 = 0x0100u;
}


namespace
{
    // Debug/test aid: PS2X_PAD_SCRIPT="sec:BUTTON[:dur],..." presses buttons at
    // fixed times (seconds since the first pad read), e.g. "30:LEFT,31:CROSS:0.5".
    struct ScriptedPress
    {
        double start = 0.0;
        double duration = 0.25;
        uint16_t mask = 0u;
    };

    uint16_t scriptButtonMask(const std::string &name)
    {
        static const struct { const char *n; uint16_t m; } kNames[] = {
            {"LEFT", 0x0080u}, {"DOWN", 0x0040u}, {"RIGHT", 0x0020u}, {"UP", 0x0010u},
            {"START", 0x0008u}, {"R3", 0x0004u}, {"L3", 0x0002u}, {"SELECT", 0x0001u},
            {"SQUARE", 0x8000u}, {"CROSS", 0x4000u}, {"CIRCLE", 0x2000u}, {"TRIANGLE", 0x1000u},
            {"R1", 0x0800u}, {"L1", 0x0400u}, {"R2", 0x0200u}, {"L2", 0x0100u}};
        for (const auto &entry : kNames)
            if (name == entry.n)
                return entry.m;
        return 0u;
    }

    const std::vector<ScriptedPress> &padScript()
    {
        static const std::vector<ScriptedPress> script = []()
        {
            std::vector<ScriptedPress> out;
            const char *env = std::getenv("PS2X_PAD_SCRIPT");
            if (!env || !*env)
                return out;
            std::string all(env);
            size_t pos = 0u;
            while (pos <= all.size())
            {
                const size_t comma = all.find(',', pos);
                const std::string item = all.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                const size_t c1 = item.find(':');
                if (c1 != std::string::npos)
                {
                    ScriptedPress press;
                    press.start = std::atof(item.substr(0, c1).c_str());
                    const size_t c2 = item.find(':', c1 + 1u);
                    press.mask = scriptButtonMask(item.substr(c1 + 1u, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1u));
                    if (c2 != std::string::npos)
                        press.duration = std::atof(item.substr(c2 + 1u).c_str());
                    if (press.mask != 0u)
                        out.push_back(press);
                }
                if (comma == std::string::npos)
                    break;
                pos = comma + 1u;
            }
            std::fprintf(stderr, "[pad-script] %zu scripted presses loaded\n", out.size());
            return out;
        }();
        return script;
    }

    uint16_t scriptedPressedMask()
    {
        const auto &script = padScript();
        if (script.empty())
            return 0u;
        static const auto t0 = std::chrono::steady_clock::now();
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        uint16_t mask = 0u;
        for (const ScriptedPress &press : script)
        {
            if (now >= press.start && now < press.start + press.duration)
                mask |= press.mask;
        }
        return mask;
    }
}

namespace
{
    // First connected gamepad (some systems expose a virtual device at index 0).
    int activeGamepad()
    {
        for (int i = 0; i < 4; ++i)
            if (IsGamepadAvailable(i))
                return i;
        return -1;
    }

    float stickDeadzone()
    {
        static const float value = []()
        {
            const char *env = std::getenv("PS2X_PAD_DEADZONE");
            const float v = env ? static_cast<float>(std::atof(env)) : 0.15f;
            return (v >= 0.0f && v < 0.9f) ? v : 0.15f;
        }();
        return value;
    }

    uint8_t stickByte(float axis)
    {
        const float dz = stickDeadzone();
        const float mag = axis < 0.0f ? -axis : axis;
        if (mag <= dz)
            return kPadStickCenter;
        float scaled = (mag - dz) / (1.0f - dz);
        if (scaled > 1.0f)
            scaled = 1.0f;
        const float signedValue = axis < 0.0f ? -scaled : scaled;
        const int value = 128 + static_cast<int>(signedValue * 127.5f);
        return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
    }
}

bool PSPadBackend::readState(int /*port*/, int /*slot*/, uint8_t *data, size_t size)
{
    if (!data || size < 32)
        return false;

    std::memset(data, 0, 32);
    data[0] = 0x01;
    data[1] = kPadAnalogMarker;
    data[2] = 0xFF;
    data[3] = 0xFF;
    data[4] = data[5] = data[6] = data[7] = kPadStickCenter;

    uint16_t pressed = 0u;
    const int gamepad = activeGamepad();

    if (gamepad >= 0)
    {
        static const struct { int button; uint16_t mask; } kButtons[] = {
            {GAMEPAD_BUTTON_LEFT_FACE_UP, PAD_UP},
            {GAMEPAD_BUTTON_LEFT_FACE_DOWN, PAD_DOWN},
            {GAMEPAD_BUTTON_LEFT_FACE_LEFT, PAD_LEFT},
            {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, PAD_RIGHT},
            {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, PAD_CROSS},
            {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, PAD_CIRCLE},
            {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, PAD_SQUARE},
            {GAMEPAD_BUTTON_RIGHT_FACE_UP, PAD_TRIANGLE},
            {GAMEPAD_BUTTON_LEFT_TRIGGER_1, PAD_L1},
            {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, PAD_R1},
            {GAMEPAD_BUTTON_LEFT_TRIGGER_2, PAD_L2},
            {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, PAD_R2},
            {GAMEPAD_BUTTON_MIDDLE_RIGHT, PAD_START},
            {GAMEPAD_BUTTON_MIDDLE_LEFT, PAD_SELECT},
            {GAMEPAD_BUTTON_LEFT_THUMB, PAD_L3},
            {GAMEPAD_BUTTON_RIGHT_THUMB, PAD_R3},
        };
        for (const auto &entry : kButtons)
            if (IsGamepadButtonDown(gamepad, entry.button))
                pressed |= entry.mask;

        // Analog triggers also count as L2/R2 on backends that only report the axis.
        if (GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_TRIGGER) > 0.3f)
            pressed |= PAD_L2;
        if (GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.3f)
            pressed |= PAD_R2;

        data[6] = stickByte(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_X));
        data[7] = stickByte(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_Y));
        data[4] = stickByte(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_X));
        data[5] = stickByte(GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_Y));
    }

    // Keyboard always works, also alongside a controller.
    static const struct { int key; uint16_t mask; } kKeys[] = {
        {KEY_UP, PAD_UP}, {KEY_W, PAD_UP},
        {KEY_DOWN, PAD_DOWN}, {KEY_S, PAD_DOWN},
        {KEY_LEFT, PAD_LEFT}, {KEY_A, PAD_LEFT},
        {KEY_RIGHT, PAD_RIGHT}, {KEY_D, PAD_RIGHT},
        {KEY_X, PAD_CROSS}, {KEY_SPACE, PAD_CROSS}, {KEY_K, PAD_CROSS},
        {KEY_C, PAD_CIRCLE}, {KEY_ESCAPE, PAD_CIRCLE}, {KEY_L, PAD_CIRCLE},
        {KEY_Z, PAD_SQUARE}, {KEY_KP_0, PAD_SQUARE}, {KEY_J, PAD_SQUARE},
        {KEY_V, PAD_TRIANGLE}, {KEY_KP_1, PAD_TRIANGLE}, {KEY_I, PAD_TRIANGLE},
        {KEY_Q, PAD_L1}, {KEY_E, PAD_R1},
        {KEY_LEFT_SHIFT, PAD_L2}, {KEY_RIGHT_SHIFT, PAD_R2},
        {KEY_ENTER, PAD_START}, {KEY_TAB, PAD_SELECT},
        {KEY_ONE, PAD_L3}, {KEY_THREE, PAD_R3},
    };
    for (const auto &entry : kKeys)
        if (IsKeyDown(entry.key))
            pressed |= entry.mask;

    pressed |= scriptedPressedMask();
    const uint16_t btns = static_cast<uint16_t>(~pressed);

    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);

    // DualShock 2 pressure bytes (right, left, up, down, triangle, circle,
    // cross, square, L1, R1, L2, R2): digital inputs report full pressure.
    static constexpr uint16_t kPressureOrder[12] = {PAD_RIGHT, PAD_LEFT, PAD_UP, PAD_DOWN,
                                                   PAD_TRIANGLE, PAD_CIRCLE, PAD_CROSS, PAD_SQUARE,
                                                   PAD_L1, PAD_R1, PAD_L2, PAD_R2};
    for (int i = 0; i < 12; ++i)
        data[8 + i] = (pressed & kPressureOrder[i]) ? 0xFFu : 0x00u;
    return true;
}
