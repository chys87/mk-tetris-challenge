#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <gflags/gflags.h>

#include "search.h"
#include "tetris_common.h"

// 最后上传成功时用的JS代码模板
inline constexpr const char* kUploadTemplate =
    "axios.post(`api/upload`, {record: '%s', score: %u}).then(({data}) => { "
    "console.log('提交结果', data); if(data.info) {console.log(data.info)} });";

inline constexpr const char* kReplayTemplate =
    "game.pause();game.playRecord('%s'.split(','));";

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto res = Solve();

  printf("Final steps: %u\n", res.final_situ.step_);
  printf("%s\n", res.final_situ.DebugString().c_str());

  // Output for genetic.py
  printf("score_by_step=%s\n", absl::StrJoin(res.score_by_step, ",").c_str());
  printf("final_score=%u\n", res.final_situ.score_);

  auto score = res.final_situ.score_;

  std::string action_str = Action::Join(res.actions);

  FILE* fp = fopen(absl::StrCat("out/", score, ".submit.js").c_str(), "w");
  fprintf(fp, kUploadTemplate, action_str.c_str(), res.final_situ.score_);
  fclose(fp);

  fp = fopen(absl::StrCat("out/", score, ".replay.js").c_str(), "w");
  fprintf(fp, kReplayTemplate, action_str.c_str());
  fclose(fp);

  return 0;
}
