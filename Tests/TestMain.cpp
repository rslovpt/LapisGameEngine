#include "TestFramework.h"

#include "Lapis/Core/Log.h"

int main() {
    Lapis::Log::SetLevel(Lapis::LogLevel::Warn);
    return LapisTest::RunAll();
}
