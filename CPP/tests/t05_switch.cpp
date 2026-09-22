#include <iostream>

int classify(int x) {
    switch (x) {
        case 1:
        case 2:
            return 100;
        case 3:
            return 300;
        default:
            return -1;
    }
}

int main() {
    for (int i = 1; i <= 4; i++) {
        std::cout << i << " -> " << classify(i) << std::endl;
    }
    return 0;
}
