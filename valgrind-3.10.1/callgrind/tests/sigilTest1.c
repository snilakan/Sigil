//Simplest test engineered to produce the communication in bytes shown in the paper.

#include "../callgrind.h"

void dummy1(int *a, int *b){
  int c, d, dummy;
  c = *a; //Transfer from main
  d = *b; //Transfer from main
  dummy = c + d;
}

void dummy2(int *a){
  int e, dummy;
  e = *a; //Transfer from main
  dummy = e - 2;
}

int sum2(int *a, int *b, int *c){
  int var1, var2;
  var1 = *a; //Transfer from intermediate
  var2 = *b; //Transfer from intermediate
  *c = var1 + var2; //Transfer to main
  return *c;
}

void summer(int *a, int *b, int *c, int *d){
  int var1, var2;
  var1 = *a; //Transfer from main
  var2 = *b; //Transfer from main
  dummy1(a,b);
  *d = sum2(&var1, &var2, c); //Transfer to difference
}

void difference(int *a, int *b, int *c){
  int var1, var2;
  var1 = 0 - *a; //Transfer from main
  var2 = *b; //Transfer from summer
  dummy2(a);
  sum2(&var1, &var2, c);
}

int main(){
  int a, b, c, d, e; //int is 4 bytes, short int is 2 bytes, a pointer is 8 bytes
  int sum, diff;

  a = 1;
  b = 2;
  summer(&a, &b, &c, &d); //c and d hold the same value
  difference(&a, &d, &e);

  sum = c;
  diff = d;
  
  return 0;
}
