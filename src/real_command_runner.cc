// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ctime>
#include <cmath>
#include <fstream>
#include <unordered_set>
#include <ctime>
#include <cmath>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "build.h"
#include "elide_middle.h"
#include "subprocess.h"

using namespace std::chrono;
using LogLine = std::tuple<pid_t, std::string, std::string>;

struct VizioLog {
  std::string FormatTargetName(std::string name);
  std::string& AddCleaningLine(std::string& data);
  std::string getLastNotEmptyLine(std::string& buffer);
  private:
    friend struct RealCommandRunner;
};

std::string& VizioLog::AddCleaningLine(std::string& data){
  return data.append(kCleanLineSymbol).append("\n");
}

std::string VizioLog::FormatTargetName(std::string name){
  std::string::size_type pos = name.find("___");
  if (pos != std::string::npos) {
    name = name.substr(0, pos);
    pos = name.rfind("_");
    if (pos != std::string::npos) {
      name = name.substr(pos+1);
    }
  }
  return name;
}

std::string VizioLog::getLastNotEmptyLine(std::string& buffer) {
  auto found = buffer.size();
  std::string currentStr = {};
  do {
    buffer = buffer.substr(0, found);
    found = buffer.rfind("\n");
    currentStr = buffer.substr(found+1);
  } while (currentStr.empty());
  // Buffer contains only one line
  if (found == std::string::npos) {
    return buffer;
  }
  // Get last part after \r
  found = currentStr.rfind("\r");
  if (found != std::string::npos) {
    currentStr = currentStr.substr(found+1);
  };

  return currentStr;
}

struct RealCommandRunner : public CommandRunner {
  explicit RealCommandRunner(const BuildConfig& config) : config_(config) {}
  virtual ~RealCommandRunner();
  size_t CanRunMore() const override;
  bool StartCommand(Edge* edge) override;
  bool WaitForCommand(Result* result) override;
  std::vector<Edge*> GetActiveEdges() override;
  void Abort() override;
  void RunLoggerProcess();
  void WatchBuildingProcess();
  void StopWatcherProcess();
  std::string CreateProgressBanner(const std::vector<LogLine>& progressBar);

  const BuildConfig& config_;
  SubprocessSet subprocs_;
  std::map<const Subprocess*, Edge*> subproc_to_edge_;
  VizioLog processLogger_;
  std::thread watcherThread_;
  std::mutex run_thread_mutex_;
  std::condition_variable run_thread_cv_;
  std::atomic_bool watcher_run_;
};

RealCommandRunner::~RealCommandRunner() {
  if (watcherThread_.joinable()) {
    watcherThread_.join();
  }
}

std::string RealCommandRunner::CreateProgressBanner(const std::vector<LogLine>& progressBar) {
  if (progressBar.empty())
    return {};

  std::string fullBanner;
  int bufferLines = progressBar.size() + 2; // The first and last line of bunner ###
  winsize size;
  if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) && size.ws_col) {
    std::string decorateLine = (std::string(size.ws_col, '#')).append("\n");
    fullBanner.append(decorateLine);
    for (auto const& [pid, name, log] : progressBar) {
      std::string log_line("# " + std::to_string(pid) + " " + name + ": " + log);
      ElideMiddleInPlace(log_line, size.ws_col);
      fullBanner.append(log_line);
    }
    fullBanner.append(decorateLine);
  }
  return fullBanner + kCleanConsoleSymbol + "\033["+ std::to_string(bufferLines) + "A";
}

void RealCommandRunner::WatchBuildingProcess() {
  while (watcher_run_) {

    std::vector<LogLine> progressBar;
    if (!subprocs_.running_.empty()) {
      for (auto &subproc : subprocs_.running_ ) {
        if (subproc->GetPID() > 0) {
          auto e = subproc_to_edge_.find(subproc);
          std::string processGoal = processLogger_.FormatTargetName(e->second->rule_->name());
          std::string message;
          switch (subproc->GetProcessStatus()) {
            case Subprocess::ALIVE: {
              std::string output = subproc->GetOutput();
              message = output.empty() ? "Is starting..." : processLogger_.getLastNotEmptyLine(output);
            }
            break;
            case Subprocess::SILENT: {
              message = "Keep silence";
            }
            break;
            case Subprocess::STUCK: {
              message = "Process keep silence more than 5 minutes. You can kill it manually or keep waiting.";
            }
            break;
            default:
              std::cout << "ERROR: Wrong Process status" << std::endl;
            break;
          }
          if (!message.empty()) {
            progressBar.push_back(make_tuple(subproc->GetPID(), processGoal, processLogger_.AddCleaningLine(message)));
          }
        }
      }
    }
    if (!progressBar.empty()) {
      std::cout << CreateProgressBanner(progressBar);
      progressBar.clear();
    }

    std::unique_lock<std::mutex> lock(run_thread_mutex_);
    run_thread_cv_.wait_for(lock, std::chrono::seconds(1), [this]{return !watcher_run_;});
  }
}

void RealCommandRunner::RunLoggerProcess() {
  // Banner anavailable in sync or quiet mode and also when build running on remote servers
  auto *env = std::getenv("NO_TTY");
  std::string no_tty = (env != nullptr) ? env : "";
  if (config_.verbosity == BuildConfig::VERBOSE && config_.enable_bufferization && no_tty != "1") {
    watcher_run_ = true;
    watcherThread_ = std::thread(&RealCommandRunner::WatchBuildingProcess, this);
  }
}

std::vector<Edge*> RealCommandRunner::GetActiveEdges() {
  std::vector<Edge*> edges;
  for (std::map<const Subprocess*, Edge*>::iterator e =
           subproc_to_edge_.begin();
       e != subproc_to_edge_.end(); ++e)
    edges.push_back(e->second);
  return edges;
}

void RealCommandRunner::StopWatcherProcess() {
  watcher_run_ = false;
  run_thread_cv_.notify_all();
}

void RealCommandRunner::Abort() {
  StopWatcherProcess();
  subprocs_.Clear();
}

size_t RealCommandRunner::CanRunMore() const {
  size_t subproc_number =
      subprocs_.running_.size() + subprocs_.finished_.size();

  int64_t capacity = config_.parallelism - subproc_number;

  if (config_.max_load_average > 0.0f) {
    int load_capacity = config_.max_load_average - GetLoadAverage();
    if (load_capacity < capacity)
      capacity = load_capacity;
  }

  if (capacity < 0)
    capacity = 0;

  if (capacity == 0 && subprocs_.running_.empty())
    // Ensure that we make progress.
    capacity = 1;

  return capacity;
}

bool RealCommandRunner::StartCommand(Edge* edge) {
  std::string command = edge->EvaluateCommand();
  std::string file_path;

  if (config_.logfiles_enabled) {
    file_path = config_.logs_dir + "/" + processLogger_.FormatTargetName(edge->rule_->name()) + ".log";

    std::ofstream logs_file(file_path);
    logs_file << "Command: " << command << "\n\n";
    logs_file.close();
  }

  Subprocess* subproc = subprocs_.Add(command, edge->use_console(), config_.enable_bufferization, file_path);
  if (!subproc)
    return false;
  subproc_to_edge_.insert(std::make_pair(subproc, edge));

  return true;
}

bool RealCommandRunner::WaitForCommand(Result* result) {
  Subprocess* subproc;
  while ((subproc = subprocs_.NextFinished()) == NULL) {
    bool interrupted = subprocs_.DoWork();
    if (interrupted)
      return false;
  }

  result->status = subproc->Finish();
  if (config_.enable_bufferization)
    result->output = subproc->GetOutput();

  std::map<const Subprocess*, Edge*>::iterator e =
      subproc_to_edge_.find(subproc);
  result->edge = e->second;
  if(!result->success()) {
    result->formatEdgeName = processLogger_.FormatTargetName(result->edge->rule_->name());
  }
  subproc_to_edge_.erase(e);

  delete subproc;
  return true;
}

CommandRunner* CommandRunner::factory(const BuildConfig& config) {
  return new RealCommandRunner(config);
}
