/* libdemo2: needs libdemo (DT_NEEDED) - dlopen loads that one too */
int demo_add(int a, int b);

int demo2_twice_sum(int a, int b) { return 2 * demo_add(a, b); }
