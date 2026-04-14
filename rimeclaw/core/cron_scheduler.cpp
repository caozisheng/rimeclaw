// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/cron_scheduler.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace rimeclaw {

// --- CronExpression ---

CronExpression::Field CronExpression::parse_field(const std::string& field,
                                                   int min, int max) {
  Field f;
  if (field == "*") return f;  // wildcard

  // Handle */N (step)
  if (field.size() > 2 && field[0] == '*' && field[1] == '/') {
    int step = std::stoi(field.substr(2));
    for (int i = min; i <= max; i += step)
      f.values.push_back(i);
    return f;
  }

  // Handle comma-separated values
  std::istringstream ss(field);
  std::string token;
  while (std::getline(ss, token, ',')) {
    auto dash = token.find('-');
    if (dash != std::string::npos) {
      int lo = std::stoi(token.substr(0, dash));
      int hi = std::stoi(token.substr(dash + 1));
      for (int i = lo; i <= hi; ++i)
        f.values.push_back(i);
    } else {
      f.values.push_back(std::stoi(token));
    }
  }
  return f;
}

bool CronExpression::field_matches(const Field& f, int value) {
  if (f.values.empty()) return true;  // wildcard
  for (int v : f.values)
    if (v == value) return true;
  return false;
}

CronExpression::CronExpression(const std::string& expr) {
  std::istringstream ss(expr);
  std::string parts[5];
  for (int i = 0; i < 5; ++i) {
    if (!(ss >> parts[i]))
      throw std::invalid_argument("Invalid cron expression: " + expr);
  }
  minute_       = parse_field(parts[0], 0, 59);
  hour_         = parse_field(parts[1], 0, 23);
  day_of_month_ = parse_field(parts[2], 1, 31);
  month_        = parse_field(parts[3], 1, 12);
  day_of_week_  = parse_field(parts[4], 0, 6);
}

bool CronExpression::Matches(const std::tm& tm) const {
  return field_matches(minute_,       tm.tm_min)  &&
         field_matches(hour_,         tm.tm_hour) &&
         field_matches(day_of_month_, tm.tm_mday) &&
         field_matches(month_,        tm.tm_mon + 1) &&
         field_matches(day_of_week_,  tm.tm_wday);
}

std::chrono::system_clock::time_point
CronExpression::NextAfter(std::chrono::system_clock::time_point after) const {
  // Advance by 1 minute and search forward up to 1 year
  auto t = after + std::chrono::minutes(1);
  for (int i = 0; i < 366 * 24 * 60; ++i) {
    time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    if (Matches(tm)) return t;
    t += std::chrono::minutes(1);
  }
  return after + std::chrono::hours(24 * 366);  // fallback
}

// --- CronJob ---

nlohmann::json CronJob::ToJson() const {
  auto next_tt = std::chrono::system_clock::to_time_t(next_run);
  return {
      {"id",          id},
      {"name",        name},
      {"schedule",    schedule},
      {"message",     message},
      {"session_key", session_key},
      {"enabled",     enabled},
      {"next_run",    static_cast<int64_t>(next_tt)},
  };
}

CronJob CronJob::FromJson(const nlohmann::json& j) {
  CronJob job;
  job.id          = j.value("id", "");
  job.name        = j.value("name", "");
  job.schedule    = j.value("schedule", "");
  job.message     = j.value("message", "");
  job.session_key = j.value("session_key", "");
  job.enabled     = j.value("enabled", true);
  int64_t next_tt = j.value("next_run", static_cast<int64_t>(0));
  job.next_run    = std::chrono::system_clock::from_time_t(
      static_cast<time_t>(next_tt));
  return job;
}

// --- CronScheduler ---

CronScheduler::CronScheduler() = default;

CronScheduler::~CronScheduler() {
  Stop();
}

void CronScheduler::Load(const std::string& filepath) {
  storage_path_ = filepath;
  std::lock_guard<std::mutex> lock(mu_);
  try {
    std::ifstream f(filepath);
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)), {});
    auto arr = nlohmann::json::parse(content);
    for (const auto& j : arr) {
      CronJob job = CronJob::FromJson(j);
      if (job.enabled &&
          job.next_run <= std::chrono::system_clock::now()) {
        CronExpression expr(job.schedule);
        job.next_run = expr.NextAfter(std::chrono::system_clock::now());
      }
      jobs_.push_back(job);
    }
    spdlog::info("CronScheduler: loaded {} jobs", jobs_.size());
  } catch (const std::exception& e) {
    spdlog::warn("CronScheduler: failed to load jobs: {}", e.what());
  }
}

void CronScheduler::Save(const std::string& filepath) const {
  std::lock_guard<std::mutex> lock(mu_);
  try {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& job : jobs_)
      arr.push_back(job.ToJson());
    std::ofstream f(filepath);
    f << arr.dump(2);
  } catch (const std::exception& e) {
    spdlog::warn("CronScheduler: failed to save jobs: {}", e.what());
  }
}

std::string CronScheduler::AddJob(const std::string& name,
                                   const std::string& schedule,
                                   const std::string& message,
                                   const std::string& session_key) {
  std::lock_guard<std::mutex> lock(mu_);
  CronJob job;
  job.id          = generate_id();
  job.name        = name;
  job.schedule    = schedule;
  job.message     = message;
  job.session_key = session_key;
  job.enabled     = true;
  CronExpression expr(schedule);
  job.next_run = expr.NextAfter(std::chrono::system_clock::now());

  jobs_.push_back(job);
  spdlog::info("CronScheduler: added job {} ({})", job.id, schedule);

  if (!storage_path_.empty()) Save(storage_path_);
  return job.id;
}

bool CronScheduler::RemoveJob(const std::string& id) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
    if (it->id == id) {
      spdlog::info("CronScheduler: removed job {}", id);
      jobs_.erase(it);
      if (!storage_path_.empty()) Save(storage_path_);
      return true;
    }
  }
  return false;
}

std::vector<CronJob> CronScheduler::ListJobs() const {
  std::lock_guard<std::mutex> lock(mu_);
  return jobs_;
}

void CronScheduler::Start(JobHandler handler) {
  if (running_) return;
  handler_ = std::move(handler);
  running_ = true;
  thread_  = std::thread(&CronScheduler::scheduler_loop, this);
  spdlog::info("CronScheduler: started");
}

void CronScheduler::Stop() {
  if (!running_) return;
  running_ = false;
  cv_.notify_one();
  if (thread_.joinable()) thread_.join();
  spdlog::info("CronScheduler: stopped");
}

void CronScheduler::scheduler_loop() {
  while (running_) {
    auto now = std::chrono::system_clock::now();
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& job : jobs_) {
        if (!job.enabled) continue;
        if (now >= job.next_run) {
          if (handler_) handler_(job);
          job.last_run = now;
          CronExpression expr(job.schedule);
          job.next_run = expr.NextAfter(now);
        }
      }
    }
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait_for(lk, std::chrono::seconds(30),
                 [this] { return !running_.load(); });
  }
}

std::string CronScheduler::generate_id() const {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dist;
  std::ostringstream ss;
  ss << std::hex << dist(gen) << dist(gen);
  return ss.str().substr(0, 12);
}

}  // namespace rimeclaw
