#include <iostream>

class Point {
public:
    int x;
    int y;
    Point() { x = 0; y = 0; }
    Point(int a, int b) { x = a; y = b; }
    int sum() { return x + y; }
    void move(int dx, int dy) { x = x + dx; y = y + dy; }
};

int main() {
    Point p1;
    Point p2(3, 4);
    std::cout << "p1 sum=" << p1.sum() << std::endl;
    std::cout << "p2 sum=" << p2.sum() << std::endl;
    p2.move(1, 1);
    std::cout << "p2 after move sum=" << p2.sum() << std::endl;
    return 0;
}
