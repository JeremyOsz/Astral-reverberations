#include <exception>
#include <iostream>

void runAstroTests();
void runDspTests();
void runMacroMappingTests();
void runMidiMappingTests();

int main()
{
    try {
        runAstroTests();
        runDspTests();
        runMacroMappingTests();
        runMidiMappingTests();
        std::cout << "Astral core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Astral core tests failed: " << error.what() << '\n';
        return 1;
    }
}

