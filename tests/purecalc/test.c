#include <stdio.h>

unsigned fibonacci(unsigned number) {
    if (number < 2) return number;
    return fibonacci(number - 1) + fibonacci(number - 2);
}

int main(int argc, char *argv[])
{
unsigned i=0;

for(;i<100;i++)//check how much time is going to take pure C program to execute it 100 times
fibonacci(34);

return 0;
}
