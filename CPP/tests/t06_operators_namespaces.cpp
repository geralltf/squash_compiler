#include <iostream>

namespace geo {
    class Vec2 {
    public:
        int x;
        int y;
        Vec2() { x = 0; y = 0; }
        Vec2(int a, int b) { x = a; y = b; }
        Vec2 operator+(Vec2 other) {
            Vec2 r(x + other.x, y + other.y);
            return r;
        }
        bool operator==(Vec2 other) {
            return x == other.x && y == other.y;
        }
    };
}

int main() {
    geo::Vec2 a(1, 2);
    geo::Vec2 b(3, 4);
    geo::Vec2 c = a + b;
    std::cout << "c=(" << c.x << "," << c.y << ")" << std::endl;

    geo::Vec2 d(4, 6);
    if (c == d) {
        std::cout << "equal" << std::endl;
    } else {
        std::cout << "not equal" << std::endl;
    }

    geo::Vec2 e = a + b + d;
    std::cout << "e=(" << e.x << "," << e.y << ")" << std::endl;
    return 0;
}
