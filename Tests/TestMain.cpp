#include "TestFramework.h"

#include "Lapis/Core/Log.h"

int main() {
    Lapis::Log::SetLevel(Lapis::LogLevel::Fatal); // tests trigger errors on purpose
    return LapisTest::RunAll();
}
