#include "test.h"
#include <iostream>
LIB(test16)

class Foo
{};

int main (int argc, char *argv[])
{
  try
    {
      throw Foo ();
    }
  catch (Foo tmp)
    {
      std::cout << "got exception" << std::endl;
    }
  return 0;
}
