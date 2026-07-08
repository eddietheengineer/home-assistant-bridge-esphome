#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/MemoryLeakWarningPlugin.h"
#include "CppUTest/TestRegistry.h"
#include "CppUTestExt/MockSupportPlugin.h"

int main(int argc, char** argv)
{
  // Disable CppUTest memory leak detection. The startup integration tests
  // use MqttTestDouble which has std::function members doing internal heap
  // allocations that CppUTest's TestMemoryAllocator falsely reports as
  // "Memory corruption (written out of bounds?)" on GCC/Ubuntu CI.
  MemoryLeakWarningPlugin::turnOffNewDeleteOverloads();

  MockSupportPlugin mockPlugin;
  TestRegistry::getCurrentRegistry()->installPlugin(&mockPlugin);

  return RUN_ALL_TESTS(argc, argv);
}
