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

#include "build.h"
#include "subprocess.h"

struct VizioLog {
  const int kDelayAfterStartCommand = 5; //log will be showed only after 5 sec
  const int kSpinnerSymbols = 4;
  const char* kWaitSpinSymb[4] = {"/", "|", "\\", "—"};
  void HideCursor();
  void ShowCursor();
  bool IsTimePassedAfterStart();
  std::string FormatTargetName(std::string name);
  std::string GetActiveEdgesInString(const std::vector<Edge*>& edges);
  void ShowActiveBuildProcess(const std::vector<Edge*>& edges);
  private:
    friend struct RealCommandRunner;
};

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

bool VizioLog::IsTimePassedAfterStart() {
  static time_t start_time = time(NULL);
  if (start_time == 0) {
    return true;
  }

  time_t curr_time = time(NULL);
  if (curr_time - start_time >= kDelayAfterStartCommand) {
    start_time = 0;
    return true;
  }
  return false;
}

void VizioLog::HideCursor() {
  printf("\e[?25l");
}

void VizioLog::ShowCursor() {
  printf("\e[?25h");
}

std::string VizioLog::GetActiveEdgesInString(const std::vector<Edge*>& edges)
{
  std::string stringOfActiveTargets;
  for (auto e = edges.begin(); e != edges.end(); ++e) {
    stringOfActiveTargets += FormatTargetName((*e)->rule_->name());
    stringOfActiveTargets += ", ";
  }
  stringOfActiveTargets.erase(stringOfActiveTargets.length()-2); //Delete last coma
  return stringOfActiveTargets;
}

void VizioLog::ShowActiveBuildProcess(const vector<Edge*>& edges) {
  HideCursor();
  if(IsTimePassedAfterStart()) {
    std::string stringOfActiveTargets = GetActiveEdgesInString(edges);
    for (int i = 0; i < kSpinnerSymbols; i++) {
      printf("Build process (%s) %s\r", stringOfActiveTargets.c_str(), kWaitSpinSymb[i]);
    }
  }
  ShowCursor();
}

struct RealCommandRunner : public CommandRunner {
  explicit RealCommandRunner(const BuildConfig& config) : config_(config) {}
  size_t CanRunMore() const override;
  bool StartCommand(Edge* edge) override;
  bool WaitForCommand(Result* result) override;
  std::vector<Edge*> GetActiveEdges() override;
  void Abort() override;

  const BuildConfig& config_;
  SubprocessSet subprocs_;
  std::map<const Subprocess*, Edge*> subproc_to_edge_;
  VizioLog processLogger_;
};

std::vector<Edge*> RealCommandRunner::GetActiveEdges() {
  std::vector<Edge*> edges;
  for (std::map<const Subprocess*, Edge*>::iterator e =
           subproc_to_edge_.begin();
       e != subproc_to_edge_.end(); ++e)
    edges.push_back(e->second);
  return edges;
}

void RealCommandRunner::Abort() {
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
  Subprocess* subproc = subprocs_.Add(command, edge->use_console());
  if (!subproc)
    return false;
  subproc_to_edge_.insert(std::make_pair(subproc, edge));

  return true;
}

bool RealCommandRunner::WaitForCommand(Result* result) {
  Subprocess* subproc;
  while ((subproc = subprocs_.NextFinished()) == NULL) {
    processLogger_.ShowActiveBuildProcess(GetActiveEdges());
    bool interrupted = subprocs_.DoWork();
    if (interrupted)
      return false;
  }

  result->status = subproc->Finish();
  result->output = subproc->GetOutput();

  std::map<const Subprocess*, Edge*>::iterator e =
      subproc_to_edge_.find(subproc);
  result->edge = e->second;
  subproc_to_edge_.erase(e);

  delete subproc;
  return true;
}

CommandRunner* CommandRunner::factory(const BuildConfig& config) {
  return new RealCommandRunner(config);
}
