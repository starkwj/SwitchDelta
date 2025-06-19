#include <iostream>
int n;
int a[100];
int sum(int *x, int n) {
    int ret = 0;
    for (int i = 0; i < n ;i++) {
        ret += x[i];
    }
    return ret;
}
int main() {
    std::cin >> n;
    for (int i = 0; i < n; i++) {
        std::cin >> a[i];
    }
    std::cout << sum(a,n);
    
}