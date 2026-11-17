#include "../Talking_Scale/DiagnosticConsole.h"
#include <cassert>
#include <cstdio>
#include <string>

ConsoleCommand command(DiagnosticConsole &console, const std::string &text) {
  ConsoleCommand result = ConsoleCommand::None;
  for (char c : text) {
    const ConsoleCommand event = console.feed(c);
    if (event != ConsoleCommand::None) result = event;
  }
  return result;
}

int main() {
  DiagnosticConsole console;
  assert(command(console, "status\r\n") == ConsoleCommand::Status);
  assert(command(console, "calibration\n") == ConsoleCommand::Calibration);
  assert(command(console, "raw\n") == ConsoleCommand::Raw);
  assert(command(console, "help\n") == ConsoleCommand::Help);
  assert(command(console, "stream on\n") == ConsoleCommand::StreamOn);
  assert(command(console, "stream off\n") == ConsoleCommand::StreamOff);
  assert(command(console, std::string(100, 'x') + "\n") == ConsoleCommand::Invalid);
  assert(command(console, "status\n") == ConsoleCommand::Status);
  assert(command(console, std::string("raw\0hidden\n", 11)) == ConsoleCommand::Invalid);
  assert(command(console, "nonsense\n") == ConsoleCommand::Invalid);

  TestSerial port;
  console.beginMessage();
  console.println("test message");
  port.space = 0;
  console.drain(port);
  assert(port.output.empty() && console.pending());
  port.space = 4;
  console.drain(port);
  assert(port.output == "test" && console.pending());
  port.space = 64;
  console.drain(port);
  assert(port.output == "test message\n" && !console.pending());
  console.beginMessage();
  console.print(std::string(250, 'x').c_str());
  port.output.clear();
  while (console.pending()) { port.space = 64; console.drain(port); }
  assert(port.output.size() == 192);
  puts("Diagnostic console tests passed.");
}
