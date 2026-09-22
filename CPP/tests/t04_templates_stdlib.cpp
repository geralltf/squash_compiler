#include <iostream>
#include <string>
#include <vector>

template<typename T>
T maxval(T a, T b) {
    if (a > b) return a;
    return b;
}

template<typename T>
class Stack {
public:
    std::vector<T> items;
    void push(T v) { items.push_back(v); }
    T pop() {
        T v = items.at(items.size() - 1);
        return v;
    }
    int count() { return items.size(); }
};

int main() {
    std::cout << "max int=" << maxval(3, 7) << std::endl;
    std::cout << "max dbl=" << maxval(3.5, 1.5) << std::endl;

    std::string s = "hello";
    s = s + " world";
    std::cout << "s=" << s << " len=" << s.length() << std::endl;

    std::vector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    std::cout << "vsize=" << v.size() << " v[1]=" << v[1] << std::endl;

    Stack<int> st;
    st.push(1);
    st.push(2);
    st.push(3);
    std::cout << "stack count=" << st.count() << " top=" << st.pop() << std::endl;

    return 0;
}
