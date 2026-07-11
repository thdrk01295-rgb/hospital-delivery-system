#include <gtest/gtest.h>

#define CONTROL_MOTOR_BRIDGE_NODE_DISABLE_MAIN
#include "../src/control_motor_bridge_node.cpp"

namespace control
{
namespace
{

TEST(ControlMotorResponseMatcherTest, IgnoresM6DoneForLiftTargetCommands)
{
  EXPECT_FALSE(isSuccessResponse("tl", "[M6] 열기 시작..."));
  EXPECT_FALSE(isSuccessResponse("tl", "[M6] 닫기 시작..."));
  EXPECT_FALSE(isSuccessResponse("tl", "[DONE] M6:OPEN"));
  EXPECT_FALSE(isSuccessResponse("tl", "[DONE] M6:CLOSE"));
  EXPECT_FALSE(isSuccessResponse("tl", "[M6] 닫기 완료."));
  EXPECT_TRUE(isSuccessResponse("tl", "[DONE] LIFT:DONE | 상단 도달"));
  EXPECT_FALSE(isSuccessResponse("3l", "[DONE] M6:CLOSE"));
  EXPECT_TRUE(isSuccessResponse("3l", "[DONE] LIFT:DONE | 도달: 310.0mm"));
}

TEST(ControlMotorResponseMatcherTest, MatchesServoByCommandedChannelAndDirection)
{
  EXPECT_TRUE(isSuccessResponse("1o", "[DONE] SERVO:1:OPEN (ch10, pulse375)"));
  EXPECT_FALSE(isSuccessResponse("1o", "[DONE] SERVO:2:OPEN (ch9, pulse375)"));
}

TEST(ControlMotorResponseMatcherTest, SeparatesLiftHomeFromEncoderReset)
{
  EXPECT_FALSE(isSuccessResponse("hl", "[LIFT] 엔코더 원점(0) 리셋."));
  EXPECT_TRUE(isSuccessResponse("hl", "[DONE] LIFT:HOME"));
  EXPECT_TRUE(isSuccessResponse("h", "[LIFT] 엔코더 원점(0) 리셋."));
}

TEST(ControlMotorResponseMatcherTest, WaitsForFinalResetLine)
{
  EXPECT_FALSE(isSuccessResponse("reset", "[HOME] 선택적 원점 정렬 완료."));
  EXPECT_TRUE(isSuccessResponse("reset", "[HOME] 초기화 완료."));
}

TEST(ControlMotorResponseMatcherTest, DoesNotUseGenericDoneFallback)
{
  EXPECT_FALSE(isSuccessResponse("unknown", "[DONE] SERVO:1:OPEN (ch10, pulse375)"));
  EXPECT_FALSE(isSuccessResponse("unknown", "[LOG] 구동 동작 정상 완료."));
}

TEST(ControlMotorResponseMatcherTest, FailureRequiresErrorPrefix)
{
  EXPECT_TRUE(isFailureResponse("[ERR] LIFT:TOP | 최대: 640.0mm"));
  EXPECT_TRUE(isFailureResponse("[ERROR] 도어(o,c)와 주행(w/s/e/d/r/f) 동시 불가."));
  EXPECT_FALSE(isFailureResponse("[INFO] ERROR text in a non-error payload"));
}

}  // namespace
}  // namespace control
