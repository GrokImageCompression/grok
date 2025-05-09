#pragma once

#include <chrono>
#include <string>

namespace grk
{

class ChronoTimer
{
public:
  ChronoTimer(std::string msg) : message(msg) {}
  void start(void)
  {
    startTime = std::chrono::high_resolution_clock::now();
  }
  void finish(void)
  {
    auto finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = finish - startTime;
    Logger::logger_.info("%s : %f ms", message.c_str(), elapsed.count() * 1000);
  }

private:
  std::string message;
  std::chrono::high_resolution_clock::time_point startTime;
};

} // namespace grk
