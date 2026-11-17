#ifndef TEST_PRINT_H
#define TEST_PRINT_H
#include <cstdint>
#include <cstddef>
#include <sstream>
#include <iomanip>
#include <string>
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t byte) = 0;
  size_t print(const char *value) {
    size_t count = 0;
    while (*value) count += write(static_cast<uint8_t>(*value++));
    return count;
  }
  size_t print(char value) { return write(static_cast<uint8_t>(value)); }
  template<class T> size_t print(T value) {
    std::ostringstream text;
    text << +value;
    return print(text.str().c_str());
  }
  size_t print(double value, int digits) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(digits) << value;
    return print(text.str().c_str());
  }
  template<class T> size_t println(T value) { return print(value) + print('\n'); }
  size_t println(double value, int digits) { return print(value, digits) + print('\n'); }
};
#endif
