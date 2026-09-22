#include <iostream>

class Shape {
public:
    virtual int area() { return 0; }
    virtual ~Shape() {}
};

class Square : public Shape {
public:
    int side;
    Square(int s) { side = s; }
    virtual int area() { return side * side; }
};

class Circle : public Shape {
public:
    int radius;
    Circle(int r) { radius = r; }
    virtual int area() { return 3 * radius * radius; }
};

void printArea(Shape *s) {
    std::cout << "area=" << s->area() << std::endl;
}

int add(int a, int b) { return a + b; }
double add(double a, double b) { return a + b; }

void addOne(int &x) { x = x + 1; }

int main() {
    Square sq(4);
    Circle c(3);
    printArea(&sq);
    printArea(&c);

    std::cout << "add int=" << add(2, 3) << std::endl;
    std::cout << "add dbl=" << add(2.5, 3.5) << std::endl;

    int v = 10;
    addOne(v);
    std::cout << "v=" << v << std::endl;

    Shape *dyn = new Square(5);
    std::cout << "dyn area=" << dyn->area() << std::endl;
    delete dyn;

    return 0;
}
