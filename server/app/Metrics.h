#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace eat::app {

class Metrics {
 public:
    void Inc(const std::string& name, std::uint64_t delta = 1);
    void SetGauge(const std::string& name, double value);
    void Observe(const std::string& name, double value);
    std::string RenderPrometheus() const;

 private:
    mutable std::mutex mutex_;
    std::map<std::string, std::uint64_t> counters_;
    std::map<std::string, double> gauges_;
    std::map<std::string, std::vector<double>> histograms_;
};

}  // namespace eat::app