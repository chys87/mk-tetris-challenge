/*
 * @Author: geek
 * @LastEditors: geek
 * @Description: 游戏页面主文件
 * @Src: https://geek.qq.com/tetris/js/page-game.js (编译前的源文件)
 */
((global) => {
  const PageGame = {
    template: '#template-game',
    components: {
      Share: global.Share,
      ReplayPanel: global.ReplayPanel,
    },
    data() {
      return {
        pageInfo: {},
        dpr: global.devicePixelRatio || 1,
        game: null,
        gameState: {
          mode: '',
          status: '',
          score: 0,
          speed: 1,
        },
        result: {
          opRecord: '',
          score: 0,
        },
        gameResVisible: false,
        uploadResVisible: false,
        uploadErrorMsg: '',
        shake: false,
        isMobile: 'ontouchend' in document,
        shareBgImgLoaded: false,
        text: {},
        leftUploadTimes: 0,
        uploading: false,
      };
    },
    created() {
      this.pageInfo = global.pageInfo;
    },
    mounted() {
      const { clientWidth: canvasW, clientHeight: canvasH } = this.$refs.canvas;
      const { Game } = global;

      this.$refs.canvas.width = canvasW * this.dpr;
      this.$refs.canvas.height = canvasH * this.dpr;
      this.game = new Game(this.$refs.canvas, {
        onStatusChange: this.onGameStateChange,
        onScoreChange: this.onGameStateChange,
        onSpeedChange: this.onSpeedChange,
        onDrop: this.onDrop,
        onEnd: this.onGameover,
      });

      this.game.start();
      global.game = this.game; // 暴露到全局，方便控制台调用 game.playRecord 方法做调试（例：game.pause();game.playRecord(['N','D19','N','D17','N','D16','N','D14','N','D11','N','D9','N','D6','N','D4','N','D3','N','D1'])）
      global.addEventListener('resize', this.resizeHandler);
    },
    methods: {
      start() {
        this.game.start();
      },
      toggleGameStatus() {
        this.game.toggleAutoRun();
      },
      async upload() {
        const { axios } = global;

        this.uploadErrorMsg = '';
        this.uploading = true;

        const { data } = await axios
          .post(`api/upload`, {
            record: this.result.opRecord.join(','),
            score: this.result.score,
          })
          .catch((err) => {
            let msg = err.message;

            if (/^timeout/i.test(msg)) {
              msg = '请求超时';
            } else if (/^network error/i.test(msg)) {
              msg = '网络错误';
            }

            this.uploading = false;
            this.uploadErrorMsg = msg;
            this.gameResVisible = false;
            this.uploadResVisible = true;
          });

        this.uploading = false;

        if (data.code !== 0) {
          this.uploadErrorMsg = data.msg;
        }

        global.beacon.onDirectUserAction('upload', {
          status: data.code === 0 ? 'success' : 'fail',
          msg: data.msg || '',
        });

        this.leftUploadTimes = data.uploadLimit || 0;
        this.gameResVisible = false;
        this.uploadResVisible = true;
      },
      onDrop() {
        this.shake = true;
        setTimeout(() => {
          this.shake = false;
        }, 60);
      },
      onGameStateChange(state) {
        Object.assign(this.gameState, state);
      },
      onSpeedChange({ speed }) {
        this.gameState.speed = speed;
      },
      onGameover({ score, opRecord }) {
        this.gameResVisible = true;
        this.result.score = score;
        this.result.opRecord = opRecord;
        this.text = this.$refs.share.getText(score);
        this.gameState.mode = this.game.mode;
        console.log('本次操作记录：', opRecord.join(','));
      },
      keydownHandler(keyCode) {
        const event = new Event('keydown');

        event.keyCode = keyCode;
        this.game.onKeyDown(event);
      },
      keyupHandler() {
        this.game.onKeyUp();
      },
      resizeHandler() {
        const { clientWidth: canvasW, clientHeight: canvasH } = this.$refs.canvas;

        this.$refs.canvas.width = canvasW * this.dpr;
        this.$refs.canvas.height = canvasH * this.dpr;
      },
      popShare(score) {
        this.game.pause();
        this.$refs.share.show(typeof score === 'number' ? score : undefined);
      },
    },
  };

  global.PageGame = PageGame;
})(window);
