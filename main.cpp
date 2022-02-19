#include <iostream>
#include "vector.h"
#include <cassert>

int main() {
    Vector<int> v;
    assert(v.Size() == 0);
    assert(v.Capacity() == 0);
}