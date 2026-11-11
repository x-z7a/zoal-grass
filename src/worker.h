// A single background thread for the slow, X-Plane-free work: indexing apt.dat files, reading airports and DSFs,
// and building pavement masks. Results are handed back through shared state the main thread polls.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zg {

class Worker {
public:
    Worker() : thread_([this] { run(); }) {}
    ~Worker() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
            jobs_.clear();
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    void post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }
    // Thread-safe log buffer; the main thread drains it into Log.txt.
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(log_mu_);
        logs_.push_back(msg);
    }
    std::vector<std::string> drain_logs() {
        std::lock_guard<std::mutex> lock(log_mu_);
        std::vector<std::string> out;
        out.swap(logs_);
        return out;
    }
    bool idle() {
        std::lock_guard<std::mutex> lock(mu_);
        return jobs_.empty() && !busy_;
    }

private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (stop_) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
                busy_ = true;
            }
            try {
                job();
            } catch (const std::exception& e) {
                log(std::string("background job failed: ") + e.what());
            } catch (...) {
                log("background job failed");
            }
            std::lock_guard<std::mutex> lock(mu_);
            busy_ = false;
        }
    }

    std::mutex mu_, log_mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::string> logs_;
    bool stop_ = false, busy_ = false;
    std::thread thread_;   // last: starts after the other members exist
};

}  // namespace zg
