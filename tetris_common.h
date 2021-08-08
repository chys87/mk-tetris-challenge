#pragma once

#include <stdint.h>

#include <array>
#include <bit>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <absl/container/inlined_vector.h>

#include "utils.h"

// 画布大小
constexpr unsigned kH = 20;
constexpr unsigned kW = 10;

inline constexpr bool XInRange(int x) { return x >= 0 && x < int(kW); }
inline constexpr bool YInRange(int y) { return y >= 0 && y < int(kH); }
inline constexpr bool YInSoftRange(int y) { return y >= -2 && y < int(kH); }
inline constexpr bool InRange(int x, int y) {
  return XInRange(x) && YInRange(y);
}

// 方块形状
enum Shape : uint8_t {
  kShapeI,
  kShapeL,
  kShapeJ,
  kShapeT,
  kShapeO,
  kShapeS,
  kShapeZ
};
constexpr int kShapes = 7;
constexpr char kShapeChars[]{"ILJTOSZ"};

struct Pos {
  int8_t x;
  int8_t y;
};

struct ShapeDesc {
  uint8_t cnt;
  Pos pos[4][4];
};

constexpr ShapeDesc kShapeDesc[]{
    {// I
     2,
     {
         {{0, 0}, {0, -1}, {0, -2}, {0, 1}},
         {{0, 0}, {1, 0}, {2, 0}, {-1, 0}},
     }},
    {// L
     4,
     {
         {{0, 0}, {0, -1}, {0, -2}, {1, 0}},
         {{0, 0}, {1, 0}, {2, 0}, {0, 1}},
         {{0, 0}, {-1, 0}, {0, 1}, {0, 2}},
         {{0, 0}, {0, -1}, {-1, 0}, {-2, 0}},
     }},
    {// J
     4,
     {
         {{0, 0}, {0, -1}, {0, -2}, {-1, 0}},
         {{0, 0}, {0, -1}, {1, 0}, {2, 0}},
         {{0, 0}, {1, 0}, {0, 1}, {0, 2}},
         {{0, 0}, {-1, 0}, {-2, 0}, {0, 1}},
     }},
    {// T
     4,
     {
         {{0, 0}, {1, 0}, {0, 1}, {-1, 0}},
         {{0, 0}, {0, -1}, {0, 1}, {-1, 0}},
         {{0, 0}, {0, -1}, {1, 0}, {-1, 0}},
         {{0, 0}, {0, -1}, {1, 0}, {0, 1}},
     }},
    {// O
     1,
     {
         {{0, 0}, {0, -1}, {1, -1}, {1, 0}},
     }},
    {// S
     2,
     {
         {{0, 0}, {0, -1}, {1, -1}, {-1, 0}},
         {{0, 0}, {-1, 0}, {-1, -1}, {0, 1}},
     }},
    {// Z
     2,
     {
         {{0, 0}, {0, -1}, {1, 0}, {-1, -1}},
         {{0, 0}, {0, -1}, {-1, 1}, {-1, 0}},
     }},
};

std::string ShapeDebugString(Shape shp, unsigned rot);

// 描述方块的x, y的范围
struct ShapeBound {
  int8_t min_x;
  int8_t max_x;
  int8_t min_y;
  int8_t max_y;
};

constexpr std::array<std::array<ShapeBound, 4>, kShapes> MakeShapeBounds() {
  std::array<std::array<ShapeBound, 4>, kShapes> res{};
  for (size_t i = 0; i < kShapes; ++i) {
    for (size_t j = 0; j < kShapeDesc[i].cnt; ++j) {
      auto& pos = kShapeDesc[i].pos[j];
      res[i][j].min_x = std::min({pos[0].x, pos[1].x, pos[2].x, pos[3].x});
      res[i][j].max_x = std::max({pos[0].x, pos[1].x, pos[2].x, pos[3].x});
      res[i][j].min_y = std::min({pos[0].y, pos[1].y, pos[2].y, pos[3].y});
      res[i][j].max_y = std::max({pos[0].y, pos[1].y, pos[2].y, pos[3].y});
    }
  }
  return res;
}

constexpr auto kShapeBounds = MakeShapeBounds();

// 描述一个动作
enum ActionType : uint8_t { kDown, kLeft, kRight, kRotate, kNew };

constexpr char kActionChars[]{"DLRCN"};

struct Action;
using ActionVector = absl::InlinedVector<Action, 4>;

struct Action {
  ActionType type{kNew};
  uint8_t by = 0;

  void AppendTo(std::string* s) const;
  static std::string Join(std::span<const Action> actions);
};

// 描述当前块的当前位置和姿势
struct BrickStatus {
  int8_t x, y;
  uint8_t rot;

  constexpr BrickStatus ReplaceX(int8_t new_x) const { return {new_x, y, rot}; }
  constexpr BrickStatus ReplaceY(int8_t new_y) const { return {x, new_y, rot}; }
  constexpr BrickStatus ReplaceRot(uint8_t new_rot) const {
    return {x, y, new_rot};
  }
};

constexpr uint32_t kSteps = 10000;

// 在编译期直接把方块序列生成出来
constexpr std::array<std::pair<Shape, BrickStatus>, kSteps> GenBricks() {
  std::array<std::pair<Shape, BrickStatus>, kSteps> res;

  constexpr uint32_t a = 27073;
  constexpr uint32_t M = 32749;
  constexpr uint32_t C = 17713;

  uint32_t cur_random_num = 12358;
  for (uint32_t i = 0; i < kSteps; ++i) {
    cur_random_num = (cur_random_num * a + C) % M;

    uint32_t weight_index = cur_random_num % 29;
    uint32_t state_index = uint32_t(i) % 4;
    uint32_t shape_index = 0;
    // I,L,J,T,O,S,Z 型方块的概率权重分别为：2,3,3,4,5,6,6（和为29）
    if (weight_index >= 0 && weight_index <= 1) {
      shape_index = 0;
    } else if (weight_index > 1 && weight_index <= 4) {
      shape_index = 1;
    } else if (weight_index > 4 && weight_index <= 7) {
      shape_index = 2;
    } else if (weight_index > 7 && weight_index <= 11) {
      shape_index = 3;
    } else if (weight_index > 11 && weight_index <= 16) {
      shape_index = 4;
    } else if (weight_index > 16 && weight_index <= 22) {
      shape_index = 5;
    } else if (weight_index > 22) {
      shape_index = 6;
    }
    res[i] = {
        Shape(uint8_t(shape_index)),
        BrickStatus{4, 0, uint8_t(state_index % kShapeDesc[shape_index].cnt)}};
  }
  return res;
}

constexpr auto kBricks = GenBricks();

// 代表一个目标位置
struct Candidate;
using CandidateVector = absl::InlinedVector<Candidate, kW>;

// 代表当前画布状态
struct Situation {
  // 用一个uint16_t的bitmask表示一行
  // 同时提供uint64_t的访问，以便在特定情形下提高性能
  union {
    static_assert(kH % 4 == 0);
    static_assert(std::endian::native == std::endian::little);
    uint64_t row_4_[kH / 4]{};
    uint16_t row_[kH];
  };
  uint32_t step_{0};
  uint32_t score_{0};
  uint32_t collapse_lines_{0};
  uint32_t collapse_count_{0};

  static constexpr uint16_t kRowBitMask = (1 << kW) - 1;

  // Accessors
  uint16_t& operator()(int y) { return row_[y]; }
  uint16_t operator()(int y) const { return row_[y]; }
  bool operator()(int x, int y) const { return row_[y] & (1u << x); }

  // 类似std::bitset::reference
  struct BitRef {
    uint16_t& storage;
    int x;
    explicit operator bool() const { return (storage & (1u << x)); }
    BitRef& operator=(bool v) {
      if (v)
        storage |= (1u << x);
      else
        storage &= ~(1u << x);
      return *this;
    }
  };
  BitRef operator()(int x, int y) { return BitRef{row_[y], x}; }

  std::string DebugString() const;

  // 总共占据的点
  unsigned TotalOccupied() const;

  // 总共占据的高度
  unsigned OccupiedHeight() const;

  // 可消掉的行的bitmask
  uint32_t CollapsableBitmask() const;

  // 指定的块是否可以放下
  bool Fits(Shape shp, BrickStatus st) const;

  // 将一个块放在指定位置，并返回新画布
  Situation PutCopy(Shape shp, BrickStatus st) const;

  // 消行，加分，前进下一步
  void CollapseInPlace();

  // 堆叠紧凑度得分
  int Quality() const;

  // 如果是明显不好的局面，返回false，直接剪掉
  bool IsOk() const;

  // 找出所有可能的动作
  void FindAllMoves(Shape st, BrickStatus initial_st,
                    CandidateVector* res) const;

  // 找路
  bool RotateRouteAppend(Shape shp, BrickStatus from, uint8_t to_rot,
                         ActionVector* res) const;
  bool HorizontalRouteAppend(Shape shp, BrickStatus from, int to_x,
                             ActionVector* res) const;
  bool AppendRouteNaive(Shape shp, BrickStatus from, BrickStatus to,
                        ActionVector* res) const;
  bool AppendRoute(Shape shp, BrickStatus from, BrickStatus to,
                   ActionVector* res, int options = 0) const;

  // 重放，用于验证，失败
  bool ReplayAndVerify(std::span<const Action> actions,
                       const Situation& target) const;

  // 判断两个Situation的方块是否一样，只比较方块，不比较step_, score_等字段
  bool BricksEqual(const Situation& other) const;

  // 比较两个Situation的方块，只比较方块，不比较step_, score_等字段
  // 大小关系无实际意义，仅为了产生一个确定性的排序
  int BricksComp(const Situation& other) const;
};

struct Candidate {
  BrickStatus st;
  Situation situ;
  ActionVector actions;
};
