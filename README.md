mk-tetris-challenge
===================

这是我参加[第四期腾讯极客技术挑战赛](https://cloud.tencent.com/developer/competition/introduction/10015)的代码。（参加的是内部赛道比赛，受主办方要求搬运一份到外网云+社区。）

#### 文件说明

* `Makefile`: Makefile
* `main.cc`: 主程序
* `search.h`, `search.cc`: 搜索和剪枝的逻辑
* `tetris_common.h`, `tetris_common.cc`: 方块掉落、旋转、消除等逻辑
* `thread_pool.h`, `thread_pool.cc`: 简易线程池
* `utils.h`: 工具类和函数
* `js`: 从题目保存下来的 JS
* `out`: 计算结果
* `genetic.py`: 遗传算法调参的代码

只在 macOS (Big Sur, Intel) 和 Linux (Gentoo amd64) 上测试过，未测试其它环境。

需要 clang 12 以上版本编译器，依赖第三方库 abseil-cpp、boost、gflags、jemalloc。运行 `make`，编译成功后会生成二进制 `main`。直接运行它，运行成功后会在 `out` 下生成 `<score>.replay.js` `<score>.submit.js` 两个文件，分别是重放和提交的 JS。在我的 MacBook Pro 上跑一次大约需要 15 分钟。

直接运行 `genetic.py` 即可使用遗传算法搜索，它会不断调用 `main` 去寻找最佳的参数，已知的最优解已经更新到 C++ 代码里的默认值。

#### 主要类型

* `Action`: 描述一个动作，如 `N`, `C1`, `L2`, `D17`
* `BrickStatus`: 描述一个正在掉落的方块的位置和方向
* `Situation`: 代表一个“局面”，即格子状态、得分、已消除行数
* `Candidate`: 一个掉落方案，即方块下落后的最终位置、下落并消行后的局面、操作序列
* `State`: 一个“状态”，对应搜索树中的一个结点，保存一个局面、得分、当前步操作序列、指向父结点的指针
* `Solution`: 最终搜索结果

#### 主要调用关系

```
- main
  |-- Solve  (算法总入口)
      |-- SearchFrom  (计算一个结点的所有子结点)
      |   |-- Situation::FindAllMoves  (计算所有合法的落点和路径)
      |   |   |-- Situation::Fits  (判断一个方块是否可以合法放在某个位置)
      |   |   |-- Situation::AppendRoute  (寻路，即寻找一个操作序列，将方块从起点移动到落点)
      |   |   |-- Situation::PutCopy  (将方块放在落点，更新局面)
      |   |   |-- Situation::CollapseInPlace  (消行，更新分数)
      |   |-- Situation::Quality  (局面评分)
      |   |-- Situation::ReplayAndVerify  (验证操作序列是否正确，目的是快速暴露 bug)
      |   |-- StateCollector::Add  (结点收集和去重)
      |-- ChooseForNextStep  (剪枝，选择进入下一轮搜索的结点)
      |   |-- MoveTopN  (从列表中选择某种指标最高的结点)
      |-- MakeSolution  (对得分最高的结点进行回溯，输出最终操作序列)
```
