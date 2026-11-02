// Euclid's algorithm by repeated subtraction (no div needed).
//   scripts/c2mov.sh examples/gcd.cpp   # -> prints 12
volatile int A = 48, B = 36;

int mo_program() {
    int a = A, b = B;
    while (a != b) {
        if (a > b) a -= b;
        else       b -= a;
    }
    return a;   // gcd(48,36) = 12
}
