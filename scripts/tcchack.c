// https://tinycc-devel.nongnu.narkive.com/Oa6OpNdS
// This presumably happens to us because skiplist calls arc4random which calls
// pthread_somethingorother which makes glibc expect this stupid symbol that tcc
// doesn't define. :(
void * __dso_handle __attribute((visibility("hidden"))) = &__dso_handle;
