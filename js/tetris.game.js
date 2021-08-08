/*
 * @Author: geek
 * @LastEditors: geek
 * @Description: 【俄罗斯方块游戏主文件】依赖 tetris.core
 * @Src: https://geek.qq.com/tetris/js/tetris.game.js (编译前的源文件)
 *
 * 游戏介绍：
 * 1、将 10000 块按固定顺序出现的方块堆叠，有消除行即得分，看谁得分高
 * 2、游戏分正式模式和回放模式，正式模式用于 PK 打榜，回放模式（playRecord）目前仅提供用于 debug 操作记录和对应的分数（暂未开放使用）
 * 3、方块下落速度会随着出现的方块数量加快，每 100 个方块后，速度递减 100ms，原始速度 1000ms，最快 100ms
 * 4、画布垂直方向满屏后，结束游戏
 * 5、方块出现的总数最大为 10000 个，超过后结束游戏
 * 6、每个方块的类型（已有：I,L,J,T,O,S,Z 型方块）、形态（各类型每旋转90度后的形态）会从配置中按照统一顺序、限定概率地读取出来，保证所有人遇到的方块顺序和方块概率都一致
 * 7、积分规则：当前方块的消除得分 = 画布中已有的格子数 * 当前方块落定后所消除行数的系数，每消除 1、2、3、4 行的得分系数依次为：1、3、6、10（例：画布当前一共有 n 个格子，当前消除行数为2，则得分为：n * 3）
 * 8、游戏结束触发规则：1)、方块落定后触顶；2)、新建方块无法放置（画布上用于放置方块的格子中有已被占用的）
 *
 * 注：游戏中优先判定是否结束游戏再计分。如：极限情况下，当前方块落定后产生了可消除行，但触顶或者超过最大方块数了，此轮不计分，直接结束游戏
 * 注：游戏使用的坐标系为 canvas 坐标系（坐标原点在左上角）详见：https://developer.mozilla.org/zh-CN/docs/Web/API/Canvas_API/Tutorial/Drawing_shapes
 *
 */
((global) => {
  const defaultPlayFreq = 1000;
  const defaultReplayFreq = 10;

  class Game {
    dpr = window.devicePixelRatio || 1; // 设备 dpr，保证在高分屏设备下高清绘制 canvas
    playFreq = defaultPlayFreq; // 游戏中每帧绘制的频率，会随着方块数增加而加快
    speed = 1; // 游戏速度展示值，会随着方块数增加而加快（取值：1到10）
    speedUpCount = 100; // 触发游戏加速的间隔方块数（每100个方块后加速一次，初始为1000ms，每次递减100ms，最快 100ms）
    replayFreq = defaultReplayFreq; // 游戏中方块下降的速度，随游玩过的增加
    record = []; // 操作记录，用于回放和成绩提交
    mode = 'play'; // 游戏播放模式，play：正常游玩模式，replay：基于操作记录的回放模式
    timer = null; // play 模式下的主逻辑 timer
    replayTimer = null; // replay 模式下的主逻辑 timer
    keydownTimer = null; // 游戏操作按键的 timer，增强移动端按压操作按钮后达到连续移动的效果

    /**
     * @description: Game 实例的构造函数
     * @param {htmlElement} canvas 画布元素
     * @param {object} opts 配置选项
     * @return {*}
     */
    constructor(canvas, opts = {}) {
      this.opts = opts;
      this.tetris = new global.Tetris(opts); // 从 Tetris 实例化 tetris 核心计算对象，核心计算全包含在此类中
      this.canvas = canvas;
      this.ctx = this.canvas.getContext('2d');
      this.bindEvents(); // 绑定键盘操作
    }

    /**
     * @description: 开始游戏
     * @param {*}
     * @return {*}
     */
    async start() {
      const { status } = this.tetris;

      if (status === 'starting') return; // 游戏已在重启中，等待重启完成，不作处理
      this.mode = 'play'; // 设置为游戏模式（另有回放模式 replay）
      await this.reset(); // 重置相关状态和数据，且播放清屏动画
      // this.play();
      this.tetris.setStatus('running'); // 设定 tetris 为 running 状态
      this.tetris.initGrids(); // 初始格子
      this.tetris.initBrick(); // 初始方块
      this.startAutoRun(true); // 启动对应模式下的 timer，自动运行游戏
    }

    /**
     * @description: 重置相关状态和数据，且播放清屏动画
     * @param {*}
     * @return {*}
     */
    async reset() {
      this.record = [];
      this.speed = 1;
      this.playFreq = defaultPlayFreq;
      this.pause(); // 清屏前先暂停当前的状态，以免清屏动画和当前主线动画冲突
      this.opts.onSpeedChange && this.opts.onSpeedChange({ speed: this.speed });

      // 重置 tetris 实例内的状态，并在回调中清屏动画
      await this.tetris.reset(async () => {
        await this.sleep(20);
        this.render();
      });
    }

    /**
     * @description: 开启自动运行游戏
     * @param {boolean} isInit 是否是初始行为
     * @return {*}
     */
    startAutoRun(isInit) {
      const { mode } = this;
      const timerId = `${mode}Timer`; // 对应的轮询 timer
      const freqId = `${mode}Freq`; // 对应的运行频率
      const stepFun = `${mode}Step`; // 单步逻辑函数，playStep 和 replayStep

      if (this.tetris.status === 'running') {
        isInit && this.render(); // 初次开启时先渲染一帧

        // 按照对应模式的频率循环执行对应单步逻辑
        this[timerId] = setTimeout(() => {
          this[stepFun]();
          this.startAutoRun();
        }, this[freqId]);
      }
    }

    /**
     * @description: 每个拟定时间间隔自动执行或者按键操作后的单步逻辑函数（位移n格，判断是否得分、触及边界等）
     * @param {string} dir 位移方向
     * @param {number} stepCount 位移步数
     * @param {boolean} needUpdate 是否需要更新
     * @return {*}
     */
    async playStep(dir = 'down', stepCount = 1, needUpdate = true) {
      // 先执行位移
      const { bottom } = this.tetris.move(dir, stepCount);
      let isStepValid = true;

      // 方块位移后触底，判定为方块落定
      if (needUpdate && bottom === 0) {
        // 方块落定后，更新状态
        const { topTouched, isRoundLimited } = this.tetris.update();

        // 触顶或者超过游戏的最大方块数量后，结束游戏
        if (topTouched || isRoundLimited) {
          const { maxBrickCount, brickCount } = this.tetris;

          this.gameOver(
            `方块是否触顶：${topTouched}（当前为第 ${brickCount} 个方块），方块数是否超过限制：${isRoundLimited}（最大方块数：${maxBrickCount}）`
          );
        } else {
          // 未触顶且未超过游戏的最大方块数量，新建方块，并判断新建的方块是否合法
          const { isValid, brickCount } = this.tetris.initBrick();

          isStepValid = isValid;

          // 新方块不合法（无法再摆放）时，结束游戏
          if (isValid) {
            this.updateSpeed();
          } else {
            this.gameOver(`已无法摆放第 ${brickCount} 个初始方块`);
          }
        }
      }

      // 单步逻辑执行后合法，绘制最新的状态到 canvas 上
      this.render(isStepValid);
    }

    /**
     * @description: 重放游戏记录（调试 record 记录可以调用此方法）
     * @param {array} record 操作历史记录，如： ['N', 'D19', 'N', 'D15', 'N', 'D14', 'N', 'D10', 'N', 'D9', 'N', 'D6', 'N', 'D4', 'N', 'D1'] 可以在界面上完整地回放一段操作记录
     * @return {*}
     */
    async playRecord(record = []) {
      const { status } = this.tetris;
      const { isValid, msg } = this.validateRecord(record);

      if (!isValid) {
        console.error(
          `${msg}\n参考格式：指令类型标识 + 多位数字（如：L1, D12, N, C2, R2，其中指令 N 不能带数字）。合法的指令标识符有：L（左移）, R（右移）, D（下降）, C（旋转）, N（新方块）`
        );
        return;
      }
      if (status === 'starting') return;
      this.mode = 'replay';
      await this.reset();
      this.record = record;
      // this.replay();
      this.tetris.setStatus('running');
      this.tetris.initGrids();
      this.startAutoRun(true);
    }

    /**
     * @description: 每个操作记录对应的重放单步逻辑（位移n格、旋转，判断是否得分、触及边界等）
     * @param {*}
     * @return {*}
     */
    async replayStep() {
      const { record } = this;

      if (record.length) {
        const curOp = record.shift(); // 当前动作类型对应的操作记录
        const [nextOp] = record; // 下一个动作类型对应的操作记录
        const opRecord = this.tetris.getOpInfo(curOp); // 包含动作类型 type 和该动作的重复次数 count，如：D2 --> { type: 'down', count: 2}
        const { type: nextType } = this.tetris.getOpInfo(nextOp);
        const curAction = this.tetris.actionMapReversed[opRecord.type];
        const nextAction = this.tetris.actionMapReversed[nextType];

        if (curAction === 'new') {
          // 构建新方块时
          const { isValid, brickCount } = this.tetris.initBrick();
          this.updateSpeed();

          // 新方块无法再摆放，结束游戏
          if (!isValid) {
            this.gameOver(`已无法摆放第 ${brickCount} 个初始方块`);
            return;
          }
        } else if (curAction && opRecord.count) {
          // 位移动作发生时
          if (['left', 'right', 'down'].indexOf(curAction) > -1) {
            this.tetris.move(curAction, 1);
          } else if (curAction === 'rotate') {
            // 旋转动作发生时
            this.tetris.rotate();
          }
          opRecord.count -= 1;
        }

        // 当前类型操作的执行次数还未执行完时，将剩余操作重新放入记录队列，待下一轮执行
        if (opRecord.count > 0) {
          record.unshift(`${opRecord.type}${opRecord.count}`);
        }

        // 当前类型操作类型执行完毕且下一类型为新建方块时，更新状态（某个方块落定）
        if (
          (opRecord.count === 0 && (nextAction === 'new' || !nextAction)) ||
          (curAction === 'new' && !record.length)
        ) {
          const { topTouched, isRoundLimited } = this.tetris.update();

          // 触顶或者超过游戏的最大方块数量后，结束游戏
          if (topTouched || isRoundLimited) {
            const { maxBrickCount, brickCount } = this.tetris;

            this.gameOver(
              `方块是否触顶：${topTouched}（当前为第 ${brickCount} 个方块），方块数是否超过限制：${isRoundLimited}（最大方块数：${maxBrickCount}）`
            );
          }
        }

        // 执行从动作后将最新状态渲染到 canvas
        this.render();
      } else {
        // 操作记录消耗完毕后，回放完毕
        this.gameOver('操作记录运算完毕');
      }
    }

    /**
     * @description: 验证操作记录的每项是否合法
     * @param {array} record 操作历史记录数组
     * @return {boolean} 该数组是否合法
     */
    validateRecord(record) {
      const ret = { isValid: true, msg: '' };
      const opCountArr = [];
      let countBetweenNN = 0;

      for (let i = 0; i < record.length; i++) {
        const { type, count } = this.tetris.getOpInfo(record[i]);

        // 第一个动作不为 N 时
        if (i === 0 && type !== 'N') {
          ret.isValid = false;
          ret.msg = '操作序列只能以 N 指令开头';
          return ret;
        }

        if (type === 'N') {
          if (i !== 0 && i !== record.length - 1) {
            opCountArr.push(countBetweenNN);
            countBetweenNN = 0;
          }
          // N 指令带数字时
          if (count !== undefined) {
            ret.isValid = false;
            ret.msg = `N 指令不能带数字（第 ${i + 1} 个指令为 ${type}${count}，请修改）`;
            return ret;
          }
        } else {
          countBetweenNN += +count;
          // 指令无法识别 或 非 N 指令不带数字时
          if (!type || (type && !count)) {
            ret.isValid = false;
            ret.msg = `存在无法识别的操作指令 或 操作指令没有带数字（第 ${i + 1} 个指令为 "${
              record[i]
            }"，请修改）`;
            return ret;
          }
        }
      }
      opCountArr.push(countBetweenNN);

      const opCountIndex = opCountArr.findIndex((val) => val === 0 || val > 100);
      if (opCountIndex > -1) {
        ret.isValid = false;
        ret.msg = `两个方块之间的操作次数必须在区间 (0,100] 内（第 ${
          opCountIndex + 1
        } 个方块的操作次数为：${opCountArr[opCountIndex]}）`;
      }

      return ret;
    }

    /**
     * @description: 更新游戏速度
     * @param {*}
     * @return {*}
     */
    updateSpeed() {
      const { speedUpCount, playFreq } = this;

      if (this.tetris.brickCount % speedUpCount === 0 && playFreq > 100) {
        this.speed += 1;
        this.playFreq -= 100;
        this.opts.onSpeedChange && this.opts.onSpeedChange({ speed: this.speed });
      }
    }

    /**  结束游戏
     * @description:
     * @param {*}
     * @return {*}
     */
    gameOver(reason = '') {
      const { opRecord, score, brickCount } = this.tetris;
      const { gridsStr, brickStr } = this.tetris.getSnapshot();

      this.stop();
      this.opts.onEnd && this.opts.onEnd({ score, brickCount, opRecord });

      const msg = `【游戏结束信息】
结束原因：${reason}
当前运行方块数：${brickCount}
当前得分：${score}
最后时刻的画布位置信息：（当最后一个砖块的位置合法时，将包含最后一个砖块在内）\n
${gridsStr}
最后时刻的砖块位置信息：
${brickStr}`;

      console.log(msg);
    }

    /**
     * @description: canvas 每帧的渲染逻辑，包含格子绘制、当前方块绘制、下一个方块的绘制
     * @param {*}
     * @return {*}
     */
    render(isBrickValid = true) {
      const vh = this.canvas.clientHeight / 100;
      const { gridConfig, curBrickInfo, grids, nextBrickRawInfo = {} } = this.tetris; // 从 tetris 计算核心实例总获取：网格配置、当前方块信息（位置和颜色）、当前所有网格的使用情况

      // 清空画布
      this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

      // 绘制当前方块
      const { pos = [], color } = curBrickInfo; // 获取当前方块的位置和颜色信息
      const gridWidth = (this.canvas.width * (3 / 4)) / gridConfig.col; // canvas 配置宽度的 3/4 用来绘制格子区域，1/4 用来绘制下一个方块
      const gridHeight = this.canvas.height / gridConfig.row;
      const gridGap = (vh / 8) * this.dpr; // 每个单元格的留白间隙
      if (isBrickValid) {
        this.renderBrick(pos, gridWidth, gridHeight, gridGap, color, []);
      }

      // 绘制下一个方块
      const { pos: nextRawPos = [], color: nextColor } = nextBrickRawInfo;
      const nextGridUnit = vh * 2 * this.dpr;
      const nextOffsetUnit = vh * 4 * this.dpr;
      const nextMarginLeft = this.canvas.width * (3 / 4) + nextOffsetUnit * 1.8;
      const nextMarginTop = nextOffsetUnit * 19;
      this.renderBrick(nextRawPos, nextGridUnit, nextGridUnit, vh / 12, nextColor, [
        nextMarginLeft,
        nextMarginTop,
      ]);

      // 绘制已占用的格子
      this.renderGrids(grids, gridWidth, gridHeight, gridGap);
    }

    /**
     * @description: 渲染当前方块
     * @param {array} pos 方块位置
     * @param {number} gridWidth 方块中每个格子的宽度
     * @param {number} gridHeight 方块中每个格子的高度
     * @param {number} gridGap 方块中格子之间的间隙
     * @param {string} color 方块颜色
     * @param {number} marginLeft 方块的左边距
     * @param {number} marginTop 方块的上边距
     * @return {*}
     */
    renderBrick(pos, gridWidth, gridHeight, gridGap, color, [marginLeft = 0, marginTop = 0]) {
      pos.length &&
        pos.forEach(([x, y]) => {
          const startX = x * gridWidth + marginLeft;
          const startY = y * gridHeight + marginTop;

          this.ctx.fillStyle = color;
          this.ctx.fillRect(
            startX + gridGap,
            startY + gridGap,
            gridWidth - gridGap * 2,
            gridHeight - gridGap * 2
          );
        });
    }

    /**
     * @description: 绘制已占用的格子
     * @param {array} grids 画布网格信息
     * @param {number} gridWidth 格子的高度
     * @param {number} gridHeight 格子的高度
     * @param {number} gridGap 格子之间的间隙
     * @return {*}
     */
    renderGrids(grids, gridWidth, gridHeight, gridGap) {
      grids.forEach((row, rowIndex) => {
        row.forEach((col, colIndex) => {
          if (col) {
            const startX = colIndex * gridWidth;
            const startY = rowIndex * gridHeight;

            this.ctx.fillStyle = col;
            this.ctx.fillRect(
              startX + gridGap,
              startY + gridGap,
              gridWidth - gridGap * 2,
              gridHeight - gridGap * 2
            );
          }
        });
      });
    }

    /**
     * @description: 绑定键盘事件
     * @param {*}
     * @return {*}
     */
    bindEvents() {
      window.addEventListener('keydown', this.onKeyDown.bind(this));
      window.addEventListener('keyup', this.onKeyUp.bind(this));
    }

    /**
     * @description: keydown 事件回调
     * @param {object} e 浏览器事件对象
     * @return {*}
     */
    onKeyDown(e) {
      this.keyDownHandler(e.keyCode);
      if (this.keydownTimer || [37, 39, 40].indexOf(e.keyCode) === -1) return;
      this.keydownTimer = setInterval(() => {
        this.keyDownHandler(e.keyCode);
      }, 150);
    }

    /**
     * @description: keyup 事件回调
     * @param {*}
     * @return {*}
     */
    onKeyUp() {
      clearInterval(this.keydownTimer);
      this.keydownTimer = null;
    }

    /**
     * @description: 键盘事件回调处理
     * @param {number} keyCode 事件类型对应的 code
     * @return {*}
     */
    keyDownHandler(keyCode) {
      const { status } = this.tetris;

      switch (keyCode) {
        // esc 键：重新开始
        case 27:
          if (status !== 'stopped') {
            this.start();
          }
          break;
        // 回车键：暂停/继续
        case 13:
          this.toggleAutoRun();
          break;
      }
      if (this.mode === 'play' && status === 'running') {
        switch (keyCode) {
          // 方向左键：左移动
          case 37:
            this.playStep('left', 1, false);
            break;

          // 方向右键：右移动
          case 39:
            this.playStep('right', 1, false);
            break;

          // 方向下键：下移动
          case 40:
            this.playStep('down', 1, false);
            break;

          // 方向上键：旋转
          case 38:
            this.tetris.rotate();
            this.render();
            break;

          // 空格键：下坠方块
          case 32:
            this.tetris.drop();
            this.playStep('down', 1);
            this.opts.onDrop();
            break;
        }
      }
    }

    /**
     * @description: sleep 阻塞函数
     * @param {number} time 需要阻塞的时间，单位：ms
     * @return {*}
     */
    async sleep(time) {
      return new Promise((resolve) => {
        setTimeout(() => {
          resolve();
        }, time);
      });
    }

    /**
     * @description: 暂停游戏
     * @param {*} isEnd 是否结束游戏
     * @return {*}
     */
    pause(isEnd) {
      const timerId = `${this.mode}Timer`;

      if (isEnd) {
        this.tetris.setStatus('stopped');
      } else {
        if (this.tetris.status === 'stopped') return; // 已经结束，就不再置为暂停了
        this.tetris.setStatus('paused');
      }

      clearTimeout(this[timerId]);
    }

    /**
     * @description: 停止游戏
     * @param {*}
     * @return {*}
     */
    stop() {
      this.pause(true);
    }

    /**
     * @description: 继续游戏
     * @param {*}
     * @return {*}
     */
    resume() {
      this.tetris.setStatus('running');
      this.startAutoRun();
    }

    /**
     * @description: 切换暂停或开始
     * @param {*}
     * @return {*}
     */
    toggleAutoRun() {
      const { status } = this.tetris;

      if (status === 'running') {
        this.pause();
      } else if (status === 'paused') {
        this.resume();
      }
    }
  }

  global.Game = Game;
})(window);
