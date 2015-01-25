#include <iostream>
using namespace std;

int addition (int a, int b)
{
  int r;
  r=a+b;
  return r;
}
int subtraction (int a, int b)
{
  int r;
  r=a-b;
  return r;
}

int mult (int a, int b)
{
  int r;
  r=a*b;
  return r;
}

int main ()
{
  int z;
  z = addition (5,3);
  std::cout << "The result is " << z<< std::endl;
  z = subtraction (8,3);
  std::cout << "The result is " << z<< std::endl;
  z = mult (7,8);
  std::cout << "The result is " << z<< std::endl;
}
