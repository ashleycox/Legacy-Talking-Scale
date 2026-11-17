#ifndef DIAGNOSTIC_CONSOLE_H
#define DIAGNOSTIC_CONSOLE_H

#include <Arduino.h>
#include <string.h>

enum class ConsoleCommand : uint8_t { None, Help, Status, Raw, Calibration, StreamOn, StreamOff, Invalid };

// Fixed buffers and a bounded drain keep USB diagnostics out of the timing path.
class DiagnosticConsole : public Print {
public:
  ConsoleCommand feed(char character) {
    if (character == '\r') return ConsoleCommand::None;
    if (character != '\n') {
      if (character < ' ' || inputLength == sizeof(input) - 1) overflow = true;
      else if (!overflow) input[inputLength++] = character;
      return ConsoleCommand::None;
    }
    input[inputLength] = 0;
    ConsoleCommand command = ConsoleCommand::Invalid;
    if (!overflow) {
      if (inputLength == 0) command = ConsoleCommand::None;
      else if (strcmp(input, "help") == 0) command = ConsoleCommand::Help;
      else if (strcmp(input, "status") == 0) command = ConsoleCommand::Status;
      else if (strcmp(input, "calibration") == 0) command = ConsoleCommand::Calibration;
      else if (strcmp(input, "raw") == 0) command = ConsoleCommand::Raw;
      else if (strcmp(input, "stream on") == 0) command = ConsoleCommand::StreamOn;
      else if (strcmp(input, "stream off") == 0) command = ConsoleCommand::StreamOff;
    }
    inputLength = 0;
    overflow = false;
    return command;
  }

  void beginMessage() { length = sent = 0; }
  size_t write(uint8_t character) override {
    if (length == sizeof(output)) return 0;
    output[length++] = character;
    return 1;
  }
  bool pending() const { return sent < length; }

  template<class Port> void drain(Port &port) {
    int budget = port.availableForWrite();
    if (budget > 16) budget = 16;
    while (budget-- > 0 && pending()) port.write(static_cast<uint8_t>(output[sent++]));
  }

private:
  char input[16] = {};
  char output[192] = {};
  uint8_t inputLength = 0;
  uint8_t length = 0;
  uint8_t sent = 0;
  bool overflow = false;
};

#endif
