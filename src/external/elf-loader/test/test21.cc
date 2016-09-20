#include <iostream>

class Base
{
 public:
  virtual ~Base () {}
};

class DerivedA : public Base
{
 public:
  virtual ~DerivedA () {}
};

class DerivedB : public Base
{
 public:
  virtual ~DerivedB () {}
};

static void Do (Base *b)
{
  DerivedB *derB =dynamic_cast<DerivedB *> (b);
  if (derB == 0)
    {
      std::cout << "yes, this is not DerivedB" << std::endl;
    }
}


int main (int argc, char *argv[])
{
  Base *b =new DerivedA ();
  Do (b);

  return 0;
}
