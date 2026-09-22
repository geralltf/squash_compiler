#include <iostream>

class Resource {
public:
    int id;
    Resource(int i) { id = i; std::cout << "construct " << id << std::endl; }
    ~Resource() { std::cout << "destruct " << id << std::endl; }
};

int greet(const char *name, int times = 2) {
    int count = 0;
    for (int i = 0; i < times; i++) {
        std::cout << "hi " << name << std::endl;
        count++;
    }
    return count;
}

int main() {
    Resource *r = new Resource(1);
    std::cout << "id=" << r->id << std::endl;
    delete r;

    int *arr = new int[5];
    for (int i = 0; i < 5; i++) arr[i] = i * i;
    std::cout << "arr[3]=" << arr[3] << std::endl;
    delete[] arr;

    int n = greet("world");
    std::cout << "n=" << n << std::endl;
    int n2 = greet("you", 1);
    std::cout << "n2=" << n2 << std::endl;

    return 0;
}
