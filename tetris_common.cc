#include "tetris_common.h"

#include <math.h>

#include <limits>

#include <absl/strings/str_cat.h>
#include <gflags/gflags.h>

std::string ShapeDebugString(Shape shp, unsigned rot) {
  char buf[5][5];
  memset(buf, ' ', sizeof(buf));
  int start_x = 2, start_y = 2;
  for (auto [dx, dy] : kShapeDesc[shp].pos[rot])
    buf[dy + start_y][dx + start_x] = '*';

  std::string res;
  for (auto& line : buf) {
    res.append(line, 5);
    res += '\n';
  }
  return res;
}

void Action::AppendTo(std::string* s) const {
  if (!s->empty()) *s += ',';
  *s += kActionChars[type];
  if (type != kNew) {
    if (by < 10) {
      *s += char('0' + by);
    } else {
      *s += char('0' + by / 10);
      *s += char('0' + by % 10);
    }
  }
}

std::string Action::Join(std::span<const Action> actions) {
  std::string res;
  if (!actions.empty()) {
    // 不能在push_back的时候就做这个合并，因为我们经常用resize将actions恢复原值
    auto it = actions.begin();
    Action hold = *it;
    while (++it != actions.end()) {
      if (hold.type == it->type) {
        hold.by += it->by;
      } else {
        hold.AppendTo(&res);
        hold = *it;
      }
    }
    hold.AppendTo(&res);
  }
  return res;
}

unsigned Situation::TotalOccupied() const {
  unsigned r = 0;
#pragma unroll
  for (uint64_t bitmask : row_4_) r += popcnt(bitmask);
  return r;
}

unsigned Situation::OccupiedHeight() const {
  for (unsigned i = 0; i < std::size(row_4_); ++i) {
    uint64_t bm4 = row_4_[i];
    if (bm4 != 0) return kH - (ctz(bm4) / 16 + i * 4);
  }
  return 0;
}

uint32_t Situation::CollapsableBitmask() const {
  uint32_t r = 0;
  for (unsigned i = 0; i < kH; ++i)
    if (row_[i] == kRowBitMask) r |= 1 << i;
  return r;
}

bool Situation::Fits(Shape shape, BrickStatus st) const {
  auto& pos = kShapeDesc[shape].pos[st.rot];
  auto& bounds = kShapeBounds[shape][st.rot];

  if (st.x + bounds.min_x < 0 || st.x + bounds.max_x >= int(kW)) return false;
  // y 只用检查max即可，min可以小于0
  if (st.y + bounds.max_y < 0 || st.y + bounds.max_y >= int(kH)) return false;

#pragma unroll
  for (const Pos& pp : pos) {
    int x = st.x + pp.x;
    int y = st.y + pp.y;
    if (y >= 0 && (*this)(x, y)) return false;
  }
  return true;
}

Situation Situation::PutCopy(Shape shape, BrickStatus st) const {
  Situation res = *this;
  for (const Pos& pp : kShapeDesc[shape].pos[st.rot]) {
    int x = st.x + pp.x;
    int y = st.y + pp.y;
    if (XInRange(x) && YInRange(y)) res(x, y) = true;
  }
  return res;
}

void Situation::CollapseInPlace() {
  ++step_;
  // 最后一个方块掉落了也不会计分，我们直接忽略。
  // 因为逻辑是先判断触顶、方块数达到最大，后判断消行
  if (step_ >= kSteps) return;

  uint32_t collapsable_bitmask = CollapsableBitmask();
  if (collapsable_bitmask != 0) {
    static const uint8_t kMul[]{1, 3, 6, 10};
    unsigned lines = popcnt(collapsable_bitmask);
    score_ += kMul[lines - 1] * TotalOccupied();
    collapse_lines_ += lines;
    collapse_count_++;

    unsigned wy = kH - 1;
    for (unsigned y = kH - 1; y > 0 /* row 0 cannot collapse */; --y) {
      if (!(collapsable_bitmask & (1 << y))) row_[wy--] = row_[y];
    }
    while (wy != unsigned(-1)) row_[wy--] = 0;
  }
}

std::string Situation::DebugString() const {
  char buffer[(kW + 2) * (kH + 2) + 128];
  char* p = buffer;

  for (unsigned x = 0; x < kW + 2; ++x) *p++ = '-';
  *p++ = '\n';
  for (unsigned y = 0; y < kH; ++y) {
    uint32_t bitmask = row_[y];
    *p++ = '|';
    for (unsigned x = 0; x < kW; ++x) *p++ = (bitmask & (1 << x)) ? '*' : ' ';
    *p++ = '|';
    *p++ = '\n';
  }
  for (unsigned x = 0; x < kW + 2; ++x) *p++ = '-';
  *p++ = '\n';

  return absl::StrCat("Step: ", step_, " Score: ", score_, "\n",
                      absl::string_view(buffer, p - buffer));
}

DEFINE_int32(quality_row_transition_penalty, 458, "");
DEFINE_int32(quality_col_transition_penalty, 0, "");
DEFINE_int32(quality_empty_penalty, 1080, "");
DEFINE_int32(quality_empty_penalty2, 0, "");

int Situation::Quality() const {
  // 格子数越多、越紧凑，得分越高
  int r = 0;

  uint32_t top_rows = 0;
  uint32_t last_row = 0;
  for (unsigned y = 0; y < kH; ++y) {
    uint32_t row = row_[y];

    // 格子越多越好
    r += 600 * popcnt(row);

    // 越紧凑越好即左右相邻两格不相同的数量越少越好
    uint32_t alts = (row ^ (row >> 1)) & (kRowBitMask >> 1);
    r -= FLAGS_quality_row_transition_penalty * popcnt(alts);

    // 上下不相同的惩罚
    r -= FLAGS_quality_col_transition_penalty * popcnt(row ^ last_row);
    last_row = row;

    // 每一个空格子如果上面有非空，减分
    uint32_t penalty = ~row & top_rows;
    r -= (FLAGS_quality_empty_penalty - FLAGS_quality_empty_penalty2) *
         popcnt(penalty);

    // 纵向累积
    top_rows |= row;
  }

  uint32_t bottom_rows = kRowBitMask;
  for (int y = kH - 1; y >= 0; --y) {
    uint32_t row = row_[y];

    // 下方有空格的砖块扣分
    uint32_t penalty = row & ~bottom_rows;
    r -= FLAGS_quality_empty_penalty2 * popcnt(penalty);

    bottom_rows &= row;
  }

  return r;
}

bool Situation::IsOk() const {
  unsigned occupied = OccupiedHeight();

  // 如果图案的形状像一条高耸的树枝，返回false
  constexpr unsigned kThresholdLines = 5;
  if (occupied >= kThresholdLines) {
    unsigned y = kH - occupied;
    unsigned max = 0;
#pragma unroll
    for (unsigned i = 0; i < kThresholdLines; ++i)
      max = std::max(max, popcnt(row_[y++]));
    if (max <= 3) return false;
  }

  return true;
}

void Situation::FindAllMoves(Shape shp, BrickStatus initial_st,
                             CandidateVector* res) const {
  res->clear();
  if (!Fits(shp, initial_st)) return;  // 放不下初始方块
  for (uint32_t rot = 0; rot < kShapeDesc[shp].cnt; ++rot) {
    uint32_t remaining_x_bitmask = kRowBitMask;
    for (unsigned y = kH - 1; y > 0; --y) {  // y=0不用考虑
      if (remaining_x_bitmask == 0) break;
      uint32_t row = row_[y];
      for (unsigned x : set_bits(remaining_x_bitmask & ~row)) {
        BrickStatus st{int8_t(x), int8_t(y), uint8_t(rot)};
        if (Fits(shp, st) && !Fits(shp, st.ReplaceY(y + 1))) {
          Candidate& cand = res->emplace_back();
          cand.st = st;
          cand.situ = PutCopy(shp, st);
          if (cand.situ(0) != 0) {
            res->pop_back();  // 碰顶算死
            continue;
          }
          if (!AppendRoute(shp, initial_st, st, &cand.actions)) {
            res->pop_back();  // 不可达
            continue;
          }
          cand.situ.CollapseInPlace();

          // 同一个x有多个有意义的y位置的可能性很小，清除掉bitmask
          remaining_x_bitmask &= ~(1 << x);
        }
      }
    }
  }
}

// 旋转
bool Situation::RotateRouteAppend(Shape shp, BrickStatus from, uint8_t to_rot,
                                  ActionVector* res) const {
  if (from.rot != to_rot) {
    uint32_t rot_cnt = kShapeDesc[shp].cnt;

    uint8_t cnt = 0;
    uint8_t rot = from.rot;
    while (rot != to_rot) {
      ++cnt;
      rot = (rot + 1) & (rot_cnt - 1);
      if (!Fits(shp, from.ReplaceRot(rot))) return false;
    }
    if (!Fits(shp, from.ReplaceRot(to_rot))) return false;
    if (cnt) res->push_back({kRotate, cnt});
  }
  return true;
}

// 水平移动
bool Situation::HorizontalRouteAppend(Shape shp, BrickStatus from, int to_x,
                                      ActionVector* res) const {
  if (from.x != to_x) {
    int delta = (to_x > from.x) ? 1 : -1;
    for (int x = from.x; x != to_x; x += delta) {
      if (!Fits(shp, from.ReplaceX(x + delta))) return false;
    }

    if (to_x > from.x) {
      res->push_back({kRight, uint8_t(to_x - from.x)});
    } else {
      res->push_back({kLeft, uint8_t(from.x - to_x)});
    }
  }
  return true;
}

// 旋转、左右移动、下移
bool Situation::AppendRouteNaive(Shape shp, BrickStatus from, BrickStatus to,
                                 ActionVector* res) const {
  if (to.y < from.y) return false;

  size_t size = res->size();

  // 先旋转，后左右移动
  if (!RotateRouteAppend(shp, from, to.rot, res) ||
      !HorizontalRouteAppend(shp, from.ReplaceRot(to.rot), to.x, res)) {
    // 先左右，后旋转
    res->resize(size);
    if (!HorizontalRouteAppend(shp, from, to.x, res) ||
        !RotateRouteAppend(shp, from.ReplaceX(to.x), to.rot, res)) {
      res->resize(size);
      return false;
    }
  }
  from.x = to.x;
  from.rot = to.rot;

  // 上下移动
  if (to.y > from.y) {
    for (int y = from.y; y != to.y; ++y)
      if (!Fits(shp, from.ReplaceY(y + 1))) {
        res->resize(size);
        return false;
      }
    res->push_back({kDown, uint8_t(to.y - from.y)});
  }

  return true;
}

// 完整的寻路
bool Situation::AppendRoute(Shape shp, BrickStatus from, BrickStatus to,
                            ActionVector* res, int options) const {
  size_t size = res->size();

  // 先尝试常规路线
  if (AppendRouteNaive(shp, from, to, res)) return true;

  // 不行？在to的左、右各考虑5个位置
  constexpr int kBottomLeftRight = 1;
  if (!(options & kBottomLeftRight)) {
    for (int dir = 0; dir < 2; ++dir) {
      for (unsigned dx = 1; dx <= 5; ++dx) {
        int x = (dir == 0) ? to.x + dx : to.x - dx;
        if (!XInRange(x)) break;
        BrickStatus via = to.ReplaceX(x);
        if (!Fits(shp, via)) break;
        if (AppendRoute(shp, from, via, res, options | kBottomLeftRight) &&
            HorizontalRouteAppend(shp, via, to.x, res))
          return true;
        res->resize(size);
      }
    }
  }

  // 不行？在from的左、右各考虑5个位置
  constexpr int kTopLeftRight = 2;
  if (!(options & kTopLeftRight)) {
    for (int dir = 0; dir < 2; ++dir) {
      for (unsigned dx = 1; dx <= 5; ++dx) {
        int x = (dir == 0) ? from.x + dx : from.x - dx;
        if (!XInRange(x)) break;
        BrickStatus via = from.ReplaceX(x);
        if (!Fits(shp, via)) break;
        if (HorizontalRouteAppend(shp, from, x, res) &&
            AppendRoute(shp, via, to, res, options | kTopLeftRight))
          return true;
        res->resize(size);
      }
    }
  }

  // 还是不行？先移动到上一个位置，再加下移试试
  if (to.y > 1) {
    BrickStatus via = to.ReplaceY(to.y - 1);
    if (Fits(shp, via)) {
      if (AppendRoute(shp, from, via, res, options) &&
          AppendRouteNaive(shp, via, to, res))
        return true;
      res->resize(size);
    }
  }

  // 还是不行? 试试t-spin?
  constexpr int kTSpin = 4;
  if (!(options & kTSpin)) {
    unsigned rot_cnt = kShapeDesc[shp].cnt;
    for (uint8_t rot = to.rot; (rot = rot ? rot - 1 : rot_cnt - 1) != to.rot;) {
      BrickStatus via = to.ReplaceRot(rot);
      if (!Fits(shp, via)) break;
      if (AppendRoute(shp, from, via, res, options | kTSpin) &&
          RotateRouteAppend(shp, via, to.rot, res))
        return true;
      res->resize(size);
    }
  }

  // 还是不行？试试先旋转一个角度，再下落
  constexpr int kInitialSpin = 8;
  if (!(options & kInitialSpin)) {
    unsigned rot_cnt = kShapeDesc[shp].cnt;
    for (uint8_t rot = from.rot;
         (rot = (rot + 1) & (rot_cnt - 1)) != from.rot;) {
      BrickStatus via = from.ReplaceRot(rot);
      if (!Fits(shp, via)) break;
      if (RotateRouteAppend(shp, from, rot, res) &&
          AppendRoute(shp, via, to, res, options | kInitialSpin))
        return true;
      res->resize(size);
    }
  }

  return false;
}

bool Situation::ReplayAndVerify(std::span<const Action> actions,
                                const Situation& target) const {
  auto shp = kBricks[step_].first;
  auto st = kBricks[step_].second;

  if (!Fits(shp, st)) {
    fprintf(stderr, "Initial block doesn't fit\n");
    return false;
  }

  for (const Action& action : actions) {
    switch (action.type) {
      default:
        fprintf(stderr, "Unknown aciton in replay\n");
        return false;
      case kNew:
        fprintf(stderr, "kNew not supported in replay\n");
        return false;
      case kRotate:
        for (unsigned i = action.by; i; --i) {
          st = st.ReplaceRot((st.rot + 1) & (kShapeDesc[shp].cnt - 1));
          if (!Fits(shp, st)) {
            fprintf(stderr, "Failed in rotation\n");
            return false;
          }
        }
        break;
      case kLeft:
        for (unsigned i = action.by; i; --i) {
          if (st.x == 0) {
            fprintf(stderr, "x is already 0 at kLeft\n");
            return false;
          }
          st = st.ReplaceX(st.x - 1);
          if (!Fits(shp, st)) {
            fprintf(stderr, "Failed in kLeft (x = %u)\n", unsigned(st.x));
            return false;
          }
        }
        break;
      case kRight:
        for (unsigned i = action.by; i; --i) {
          if (unsigned(st.x) >= kW - 1) {
            fprintf(stderr, "x is already %u at kRight\n", kW - 1);
            return false;
          }
          st = st.ReplaceX(st.x + 1);
          if (!Fits(shp, st)) {
            fprintf(stderr, "Failed in kRight (x = %u)\n", unsigned(st.x));
            return false;
          }
        }
        break;
      case kDown:
        for (unsigned i = action.by; i; --i) {
          if (unsigned(st.y) >= kH - 1) {
            fprintf(stderr, "y is already %u at kDown\n", kH - 1);
            return false;
          }
          st = st.ReplaceY(st.y + 1);
          if (!Fits(shp, st)) {
            fprintf(stderr, "Failed in kDown (x = %u)\n", unsigned(st.x));
            return false;
          }
        }
        break;
    }
  }

  Situation new_st = PutCopy(shp, st);
  new_st.CollapseInPlace();
  if (!new_st.BricksEqual(target)) {
    fprintf(stderr, "Final situations are different:\n%s\n%s",
            new_st.DebugString().c_str(), target.DebugString().c_str());
    return false;
  }
  return true;
}

bool Situation::BricksEqual(const Situation& other) const {
  for (size_t i = 0; i < std::size(row_4_); ++i)
    if (row_4_[i] != other.row_4_[i]) return false;
  return true;
}

int Situation::BricksComp(const Situation& other) const {
  for (size_t i = 0; i < std::size(row_4_); ++i)
    if (row_4_[i] != other.row_4_[i])
      return row_4_[i] > other.row_4_[i] ? 1 : -1;
  return 0;
}
