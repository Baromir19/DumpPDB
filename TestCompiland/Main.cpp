#include <Test/Tests.hpp>
#include <Rtti/RttiTests.hpp>

void main()
{
    Test::CompileTested test = Test::CompileTested();

    // Keeps the EXE/RTTI fixture (vftables, RTTI records, constructors,
    // destructors and operator-new sites) in the linked image.
    Test::Rtti::touchAllObjects();
}