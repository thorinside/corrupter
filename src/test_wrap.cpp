#include <iostream>

inline double WrapPositive(double x, double n) {
  if (n <= 0.0) {
    return 0.0;
  }
  while (x >= n) {
    x -= n;
  }
  while (x < 0.0) {
    x += n;
  }
  return x;
}

int main() {
    double t = -1e-16;
    double n = 100.0;
    double w = WrapPositive(t, n);
    std::cout << "t: " << t << " w: " << w << " (w == n): " << (w == n) << std::endl;
    return 0;
}
