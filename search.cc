#include "search.h"

#include <sys/resource.h>

#include <atomic>
#include <chrono>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/str_split.h>
#include <boost/intrusive_ptr.hpp>
#include <gflags/gflags.h>

#include "tetris_common.h"
#include "thread_pool.h"

DEFINE_int32(total_keep, 9041, "每一层选出结点总数量");
DEFINE_double(score_keep_ratio, 0.163, "选出的结点中按分数的比例");
DEFINE_double(score_height_quota, 0.210, "砖块高度配额(score)");
DEFINE_string(score_parent_quota, "0.3,0.5,0.7,0.9", "砖块祖先配额(score)");
DEFINE_double(quality_height_quota, 0.355, "砖块高度配额(quality)");
DEFINE_string(quality_parent_quota, "0.3,0.5,0.7,0.9", "砖块祖先配额(quality)");
DEFINE_int32(ignore_score_threshold, 2200, "分数剪枝条件");
DEFINE_int32(ignore_height_threshold, 6, "高度剪枝条件");

DEFINE_string(abort_threshold, "", "在指定步数的最低分如果低于阈值，直接退出");

// 根据flags计算出来的
unsigned g_score_keep_count;
unsigned g_quality_keep_count;
std::vector<unsigned> g_score_parent_quota;
std::vector<unsigned> g_quality_parent_quota;
unsigned g_abort_threshold[kSteps]{};

void PrepareFlags() {
  g_quality_keep_count = FLAGS_total_keep * (1. - FLAGS_score_keep_ratio);
  g_score_keep_count = FLAGS_total_keep - g_quality_keep_count;

  for (auto part : absl::StrSplit(FLAGS_score_parent_quota, ",")) {
    float x;
    if (absl::SimpleAtof(part, &x))
      g_score_parent_quota.push_back(g_score_keep_count * x);
  }
  for (auto part : absl::StrSplit(FLAGS_quality_parent_quota, ",")) {
    float x;
    if (absl::SimpleAtof(part, &x))
      g_quality_parent_quota.push_back(g_quality_keep_count * x);
  }

  for (unsigned i = 0; auto part : absl::StrSplit(FLAGS_abort_threshold, ",")) {
    static_cast<void>(absl::SimpleAtoi(part, &g_abort_threshold[i]));
    if (++i >= kSteps) break;
  }
}

struct State;
using StatePtr = boost::intrusive_ptr<State>;

struct State {
  Situation situ;                                   // 当前局面
  int quality{situ.Quality()};                      // 缓存situ.Quality()
  unsigned occupied_height{situ.OccupiedHeight()};  // 缓存situ.OccupiedHeight()
  StatePtr parent;                                  // 父结点
  ActionVector actions;                             // 操作序列

  // boost::intrusive_ptr使用的引用计数
  mutable std::atomic<unsigned> ref_cnt_{0};

  friend void intrusive_ptr_add_ref(const State* x) {
    x->ref_cnt_.fetch_add(1, std::memory_order_acq_rel);
  }
  friend void intrusive_ptr_release(const State* x) {
    if (x->ref_cnt_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) delete x;
  }
};

uint64_t FastHashBricks(const Situation& situ) {
  uint64_t h = 0;
#pragma unroll
  for (uint64_t x : situ.row_4_) h = (h << kW | h >> (64 - kW)) ^ x;
  return h;
}

struct BricksHasher {
  size_t operator()(const Situation& situ) const {
    size_t h = 0;
    for (auto v : situ.row_4_) HashCombine(h, v);
    return h;
  }

  uint64_t operator()(const StatePtr& state_ptr) const {
    return (*this)(state_ptr->situ);
  }
};

struct BricksEqual {
  bool operator()(const Situation& a, const Situation& b) const {
    return a.BricksEqual(b);
  }
  bool operator()(const StatePtr& a, const StatePtr& b) const {
    return (*this)(a->situ, b->situ);
  }
};

// 收集下一层的结点，并进行去重
class StateCollector {
 public:
  void Add(StatePtr&& state_ptr) {
    auto& situ = state_ptr->situ;
    size_t i = FastHashBricks(situ) % kN;
    std::lock_guard lock(mutexes_[i]);
    auto& set = sets_[i];
    auto better_than = [](const Situation& a, const Situation& b) {
      return (a.score_ > b.score_) ||
             (a.score_ == b.score_ && a.collapse_count_ < b.collapse_count_);
    };
    auto [it, ok] = set.insert(state_ptr);
    if (!ok) {
      if (better_than(state_ptr->situ, (*it)->situ))
        const_cast<StatePtr&>(*it) = std::move(state_ptr);
    }
  }

  void MoveTo(std::vector<StatePtr>* res) {
    for (auto& set : sets_) {
      for (auto& item : set) res->push_back(std::move(item));
      set.clear();
    }
  }

 private:
  static constexpr size_t kN = 17;
  absl::flat_hash_set<StatePtr, BricksHasher, BricksEqual> sets_[kN];
  std::mutex mutexes_[kN];
};

void SearchFrom(StatePtr& state_ptr, StateCollector* res);
Solution MakeSolution(State* final_state,
                      const std::vector<unsigned>& score_by_step);

void ChooseForNextStep(std::vector<StatePtr>&& orig,
                       std::vector<StatePtr>* res);

// 算法主入口
Solution Solve() {
  PrepareFlags();

  StatePtr initial_state{new State};

  std::vector<StatePtr> step_bests{initial_state};
  StatePtr global_best{initial_state};

  ThreadPool thread_pool;

  std::vector<unsigned> score_by_step;
  auto start_time = std::chrono::steady_clock::now();

  for (uint32_t step = 0; step < kSteps; ++step) {
    StateCollector collector;

    for (StatePtr& state_ptr : step_bests) {
      if (state_ptr->situ.step_ != step) {
        fprintf(stderr, "Step error ! %u != %u\n", state_ptr->situ.step_, step);
        return {};
      }
    }

    thread_pool.SyncRunSpan(std::span(step_bests), [&](StatePtr& state_ptr) {
      SearchFrom(state_ptr, &collector);
    });

    std::vector<StatePtr> next_step_bests;
    collector.MoveTo(&next_step_bests);

    auto global_best_key_func = [](const State& state) {
      return std::make_tuple(state.situ.score_, state.situ.step_,
                             state.quality);
    };
    auto global_best_key = global_best_key_func(*global_best);
    for (StatePtr& state_ptr : next_step_bests) {
      auto new_key = global_best_key_func(*state_ptr);
      if (new_key > global_best_key ||
          (new_key == global_best_key &&
           state_ptr->situ.BricksComp(global_best->situ) > 0)) {
        global_best = state_ptr;
        global_best_key = new_key;
      }
    }

    ChooseForNextStep(std::move(next_step_bests), &step_bests);

    unsigned current_best_score = global_best->situ.score_;
    if (current_best_score < g_abort_threshold[step]) return Solution();
    score_by_step.push_back(current_best_score);

    if (step != 0 && step % 100 == 0) {
      rusage ru;
      getrusage(RUSAGE_SELF, &ru);

      uint32_t cpu_ms = ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000;
      uint32_t wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start_time)
                             .count();
      fprintf(
          stderr,
          "==============================================\n"
          "Step %u (abort threshold %u; estimated final score %u; "
          "CPU parallel %.1f; %u ms / step; ETA %u s of %u s):\n%s",
          step, g_abort_threshold[step],
          uint32_t(uint64_t(global_best->situ.score_) * kSteps / (step + 1)),
          float(cpu_ms) / float(wall_ms), wall_ms / (step + 1),
          uint32_t(uint64_t(wall_ms) * (kSteps - step - 1) / (step + 1) / 1000),
          uint32_t(uint64_t(wall_ms) * kSteps / (step + 1) / 1000),
          global_best->situ.DebugString().c_str());
    }
  }

  return MakeSolution(global_best.get(), score_by_step);
}

void SearchFrom(StatePtr& state_ptr, StateCollector* res) {
  thread_local CandidateVector vec;
  vec.clear();
  const State* state = state_ptr.get();

  auto [shp, initial_st] = kBricks[state_ptr->situ.step_];
  state->situ.FindAllMoves(shp, initial_st, &vec);

  auto initial_height = state_ptr->occupied_height;
  auto initial_collapse_lines = state_ptr->situ.collapse_lines_;
  auto initial_occupied = state_ptr->situ.TotalOccupied();

  for (Candidate& cand : vec) {
    // 高度太低或砖块太少时，禁止消除
    if (auto collapsed = cand.situ.collapse_lines_ - initial_collapse_lines;
        collapsed >= 1 && collapsed <= 4) {
      static const unsigned kThresholdHeight[]{
          kH - 4,
          kH - 4,
          kH - 3,
          kH - 3,
      };
      static const unsigned kThresholdOccupied[]{
          (kH - 6) * (kW - 1),
          (kH - 6) * (kW - 1),
          (kH - 5) * (kW - 1),
          (kH - 5) * (kW - 1),
      };
      if (initial_height < kThresholdHeight[collapsed - 1] ||
          initial_occupied < kThresholdOccupied[collapsed - 1])
        continue;
    }

    // 按IsOk剪枝
    if (!cand.situ.IsOk()) continue;

    if (!state_ptr->situ.ReplayAndVerify(cand.actions, cand.situ)) {
      fprintf(stderr, "Verification failed:\n%s\n%s\n%s\n%s\n",
              ShapeDebugString(shp, initial_st.rot).c_str(),
              state_ptr->situ.DebugString().c_str(),
              Action::Join(cand.actions).c_str(),
              cand.situ.DebugString().c_str());
      exit(1);
    }

    auto quality = cand.situ.Quality();
    auto occupied_height = cand.situ.OccupiedHeight();
    res->Add(StatePtr{new State{std::move(cand.situ), quality, occupied_height,
                                state_ptr, std::move(cand.actions)}});
  }
}

// 将from中按key_func计算的最高n个元素移动到to里面
// ancestor_quotas
// 控制选出的结点的多样性（列表不要过快被来自同一祖先的结点垄断）
template <typename Callback>
void MoveTopN(std::vector<StatePtr>& from, std::vector<StatePtr>* to,
              unsigned n, std::span<unsigned> ancestor_max, uint32_t height_max,
              Callback key_func) {
  if (n == 0) return;
  if (from.size() <= n) {
    *to = std::move(from);
    from.clear();
    return;
  }

  std::sort(from.begin(), from.end(),
            [&](const StatePtr& a, const StatePtr& b) {
              auto u = key_func(a), v = key_func(b);
              if (u != v) return u > v;
              // 产生一个确定性的排序
              return a->situ.BricksComp(b->situ) > 0;
            });

  using Value = std::remove_cvref_t<decltype(key_func(from[0]))>;

  struct ParentQuotaInfo {
    unsigned cnt{0};
    Value value{};
  };
  absl::flat_hash_map<State*, ParentQuotaInfo> quota_map;

  ParentQuotaInfo height_quota_map[kH];

  auto quota_check = [&](const ParentQuotaInfo& info, const Value& value,
                         unsigned max) -> bool {
    // 如果value和最后一个插入的相等，也保留
    return (info.cnt < max || value == info.value);
  };

  auto parent_quota_check = [&](const StatePtr& parent_ptr, const Value& value,
                                unsigned max) -> bool {
    if (!parent_ptr) return true;
    auto& info = quota_map[parent_ptr.get()];
    return quota_check(info, value, max);
  };

  std::vector<StatePtr> res_buffer;
  for (auto& state_ptr : from) {
    State* node = state_ptr.get();
    auto value = key_func(state_ptr);
    bool skip = false;
    for (unsigned max : ancestor_max) {
      if (!node->parent) break;
      if (!parent_quota_check(node->parent, value, max)) {
        skip = true;
        break;
      }
      node = node->parent.get();
    }
    if (skip) continue;

    auto& height_info = height_quota_map[state_ptr->occupied_height];
    if (!quota_check(height_info, value, height_max)) continue;

    // 即使n已经到0，如果它和最后一个相待，也保留
    if (n == 0 && (res_buffer.empty() ||
                   key_func(state_ptr) != key_func(res_buffer.back())))
      break;
    if (n) --n;

    // 现在才给各处info的cnt真正加上
    for (State* node = state_ptr.get(); unsigned max : ancestor_max) {
      static_cast<void>(max);  // Supress warning
      if (!node->parent) break;
      quota_map[node->parent.get()].cnt++;
      node = node->parent.get();
    }
    height_info.cnt++;

    res_buffer.push_back(std::move(state_ptr));
    state_ptr.reset();  // Make sure it becomes nullptr
  }
  std::erase_if(from, [](StatePtr& state_ptr) { return !state_ptr; });

  for (auto& state_ptr : res_buffer) to->push_back(std::move(state_ptr));
}

// 保留State的策略
void ChooseForNextStep(std::vector<StatePtr>&& orig,
                       std::vector<StatePtr>* res) {
  res->clear();
  if (orig.empty()) return;

  // 剪掉score比最大值小太多的，高度比最高值小太多的
  uint32_t max_score = 0;
  uint32_t max_height = 0;
  for (StatePtr& state_ptr : orig) {
    auto& situ = state_ptr->situ;
    max_score = std::max(max_score, situ.score_);
    max_height = std::max(max_height, state_ptr->occupied_height);
  }
  std::erase_if(orig, [max_score, max_height](const StatePtr& state_ptr) {
    return state_ptr->situ.score_ + FLAGS_ignore_score_threshold < max_score ||
           state_ptr->occupied_height + FLAGS_ignore_height_threshold <
               max_height;
  });

  // quality最高的，分数最高的各保留一些

  if (orig.size() <= g_quality_keep_count + g_score_keep_count) {
    res->swap(orig);
    return;
  }

  // 先取每次消除平均得分最高的
  MoveTopN(orig, res, g_score_keep_count, g_score_parent_quota,
           g_score_keep_count * FLAGS_score_height_quota,
           [](const StatePtr& state_ptr) {
             auto& situ = state_ptr->situ;
             return std::make_tuple(
                 uint64_t(situ.score_) * 10000 /
                     std::max<uint32_t>(situ.collapse_count_, 1),
                 situ.score_, state_ptr->quality);
           });

  // 再取quality最好的
  MoveTopN(orig, res, g_quality_keep_count, g_quality_parent_quota,
           g_quality_keep_count * FLAGS_quality_height_quota,
           [](const StatePtr& state_ptr) {
             return std::make_pair(state_ptr->quality, state_ptr->situ.score_);
           });
}

Solution MakeSolution(State* state,
                      const std::vector<unsigned>& score_by_step) {
  Solution res;
  res.final_situ = state->situ;
  res.score_by_step = score_by_step;

  while (state && state->situ.step_ >= 1) {
    res.actions.insert(res.actions.end(), state->actions.rbegin(),
                       state->actions.rend());
    res.actions.push_back({kNew});
    state = state->parent.get();
  }
  std::reverse(res.actions.begin(), res.actions.end());
  return res;
}
