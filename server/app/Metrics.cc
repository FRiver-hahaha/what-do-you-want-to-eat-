#include "server/app/Metrics.h"

#include <algorithm>
#include <sstream>

namespace eat::app {
namespace {

double Quantile(std::vector<double> values, double q) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(q * static_cast<double>(values.size() - 1));
    return values[index];
}

}  // namespace

void Metrics::Inc(const std::string& name, std::uint64_t delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] += delta;
}

void Metrics::SetGauge(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name] = value;
}

void Metrics::Observe(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& values = histograms_[name];
    values.push_back(value);
    if (values.size() > 4096) {
        values.erase(values.begin(), values.begin() + 1024);
    }
}

std::string Metrics::RenderPrometheus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    for (const auto& item : counters_) {
        out << "eat_" << item.first << " " << item.second << "\n";
    }
    for (const auto& item : gauges_) {
        out << "eat_" << item.first << " " << item.second << "\n";
    }
    for (const auto& item : histograms_) {
        const auto& values = item.second;
        if (values.empty()) continue;
        out << "eat_" << item.first << "_p50 " << Quantile(values, 0.50) << "\n";
        out << "eat_" << item.first << "_p95 " << Quantile(values, 0.95) << "\n";
        out << "eat_" << item.first << "_p99 " << Quantile(values, 0.99) << "\n";
    }
    return out.str();
}

}  // namespace eat::app