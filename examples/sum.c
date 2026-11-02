// A loop gcc keeps in registers at -O1 (so it stays within the v1 subset).
// `n` is volatile so the loop is not constant-folded away, which also makes
// the translator exercise a real memory load.
//
//   scripts/c2mov.sh examples/sum.c       # -> prints 55
volatile int n = 10;

int mo_program(void) {
    int s = 0;
    for (int i = 1; i <= n; i++) s += i;
    return s;   // sum 1..10 = 55
}
