#include <stdio.h>


int main(){
volatile long x = 0;
while (x < 100000000) {
    x++;
}
printf("%ld\n", x);
return 0;
}
