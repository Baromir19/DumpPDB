#pragma once

// #define ENABLE_TEST_COMPILATION

#ifdef ENABLE_TEST_COMPILATION
	#define COMPILE_TEST Test::CompileTested __testedCompiland = Test::CompileTested();
#else
	#define COMPILE_TEST do {} while (0);
#endif

namespace Test
{
	typedef void(*TestedTypedef)(void*);

	struct TestedStruct
	{
	public:
		unsigned short testedField_SHORT;
	};

	class TestedClass
	{
	protected:
		class TestedSubClass
		{
			int testedField_INT;
		};

	public:
		unsigned short testedField_SHORT;
		unsigned int testedFunc(TestedTypedef* _arg1) { return 0; }
	};

	unsigned int compileTestedFunc(TestedTypedef* _arg1) { return 0; }
	bool compileVariable = true;

	class CompileTested
	{
		TestedTypedef compile_TYPEDEF;
		TestedStruct compile_STRUCT;
		TestedClass compile_CLASS;

	public:
		CompileTested()
		{
			compile_STRUCT.testedField_SHORT = 0x1234;
			compile_CLASS.testedField_SHORT = 0x5678;
			compile_CLASS.testedFunc(&compile_TYPEDEF);
			compileVariable = true;
		}
	};
}