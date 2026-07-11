// Copyright 2026 Hospital Delivery System
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

#include <cstdlib>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>  // NOLINT(build/include_order)
#include <rclcpp/rclcpp.hpp>

#include "robot_task/task_motor_sequence_node.hpp"

namespace robot_task
{
namespace
{

class TaskMotorSequenceNodeTest : public ::testing::Test
{
protected:
  using ExecuteMotorSequence = TaskMotorSequenceNode::ExecuteMotorSequence;

  static void SetUpTestSuite()
  {
    setenv("ROS_LOG_DIR", "/tmp/ros-log", 0);
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }

  static void TearDownTestSuite()
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }

  void SetUp() override
  {
    node_ = std::make_shared<TaskMotorSequenceNode>();
    configureMotorCommands();
  }

  void configureMotorCommands()
  {
    node_->set_parameter(rclcpp::Parameter("lift_level_1_command", "1l"));
    node_->set_parameter(rclcpp::Parameter("lift_level_2_command", "2l"));
    node_->set_parameter(rclcpp::Parameter("lift_level_3_command", "3l"));
    node_->set_parameter(rclcpp::Parameter("lift_top_command", "tl"));
    node_->set_parameter(rclcpp::Parameter("lift_home_command", "hl"));
    node_->set_parameter(rclcpp::Parameter("top_clothes_step_forward_command", "w"));
    node_->set_parameter(rclcpp::Parameter("bottom_clothes_step_forward_command", "e"));
    node_->set_parameter(rclcpp::Parameter("kit_step_forward_command", "r"));
    node_->set_parameter(rclcpp::Parameter("door_open_command", "o"));
    node_->set_parameter(rclcpp::Parameter("door_close_command", "c"));
  }

  ExecuteMotorSequence::Goal rentalGoal(int order_top, int order_bottom)
  {
    ExecuteMotorSequence::Goal goal;
    goal.task_id = 1;
    goal.task_type = "patient_clothes_rental";
    goal.stop_type = "destination";
    goal.phase = "PREPARE";
    goal.order_top = order_top;
    goal.order_bottom = order_bottom;
    return goal;
  }

  ExecuteMotorSequence::Goal kitDeliveryGoal()
  {
    ExecuteMotorSequence::Goal goal;
    goal.task_id = 2;
    goal.task_type = "kit_delivery";
    goal.stop_type = "destination";
    goal.phase = "PREPARE";
    return goal;
  }

  ExecuteMotorSequence::Goal goalFor(
    const std::string & task_type,
    const std::string & stop_type,
    const std::string & phase)
  {
    ExecuteMotorSequence::Goal goal;
    goal.task_id = 3;
    goal.task_type = task_type;
    goal.stop_type = stop_type;
    goal.phase = phase;
    return goal;
  }

  std::vector<std::string> commandsFor(const ExecuteMotorSequence::Goal & goal)
  {
    const auto result = node_->buildAndValidateSequenceForTest(goal);
    EXPECT_TRUE(result.success) << result.message;

    std::vector<std::string> commands;
    commands.reserve(result.steps.size());
    for (const auto & step : result.steps) {
      commands.push_back(step.command);
    }
    return commands;
  }

  bool containsAnyReverseCommand(const std::vector<std::string> & commands) const
  {
    return std::find(commands.begin(), commands.end(), "s") != commands.end() ||
           std::find(commands.begin(), commands.end(), "d") != commands.end() ||
           std::find(commands.begin(), commands.end(), "f") != commands.end();
  }

  std::shared_ptr<TaskMotorSequenceNode> node_;
};

TEST_F(TaskMotorSequenceNodeTest, TopRentalUsesM1ForwardOnly)
{
  const auto commands = commandsFor(rentalGoal(1, 0));

  EXPECT_EQ(commands, (std::vector<std::string>{"o", "1l", "w", "tl"}));
  EXPECT_NE(std::find(commands.begin(), commands.end(), "w"), commands.end());
  EXPECT_EQ(std::find(commands.begin(), commands.end(), "e"), commands.end());
  EXPECT_FALSE(containsAnyReverseCommand(commands));
}

TEST_F(TaskMotorSequenceNodeTest, BottomRentalUsesM2ForwardOnly)
{
  const auto commands = commandsFor(rentalGoal(0, 1));

  EXPECT_EQ(commands, (std::vector<std::string>{"o", "2l", "e", "tl"}));
  EXPECT_NE(std::find(commands.begin(), commands.end(), "e"), commands.end());
  EXPECT_EQ(std::find(commands.begin(), commands.end(), "w"), commands.end());
  EXPECT_FALSE(containsAnyReverseCommand(commands));
}

TEST_F(TaskMotorSequenceNodeTest, TopAndBottomRentalRunsM1BeforeM2)
{
  const auto commands = commandsFor(rentalGoal(1, 1));

  EXPECT_EQ(
    commands,
    (std::vector<std::string>{"o", "1l", "w", "e", "tl"}));
  EXPECT_EQ(std::find(commands.begin(), commands.end(), "2l"), commands.end());
  EXPECT_EQ(std::count(commands.begin(), commands.end(), "tl"), 1);
  EXPECT_EQ(commands.back(), "tl");
  const auto top_command = std::find(commands.begin(), commands.end(), "w");
  const auto bottom_command = std::find(commands.begin(), commands.end(), "e");
  ASSERT_NE(top_command, commands.end());
  ASSERT_NE(bottom_command, commands.end());
  EXPECT_LT(
    std::distance(commands.begin(), top_command),
    std::distance(commands.begin(), bottom_command));
  EXPECT_FALSE(containsAnyReverseCommand(commands));
}

TEST_F(TaskMotorSequenceNodeTest, KitDeliveryKeepsM3ForwardCommand)
{
  const auto commands = commandsFor(kitDeliveryGoal());

  EXPECT_EQ(commands, (std::vector<std::string>{"3l", "r", "tl"}));
  EXPECT_FALSE(containsAnyReverseCommand(commands));
}

TEST_F(TaskMotorSequenceNodeTest, SupportedTaskPhaseLookupTableIsComplete)
{
  struct SequenceCase
  {
    std::string task_type;
    std::string stop_type;
    std::string phase;
    std::vector<std::string> commands;
  };

  const std::vector<SequenceCase> cases = {
    {"clothes_refill", "destination", "PREPARE", {"4o", "3o"}},
    {"clothes_refill", "destination", "FINALIZE", {"4c", "3c"}},
    {"kit_refill", "destination", "PREPARE", {"2o"}},
    {"kit_refill", "destination", "FINALIZE", {"2c"}},
    {"kit_delivery", "destination", "PREPARE", {"3l", "r", "tl"}},
    {"kit_delivery", "destination", "FINALIZE", {"hl"}},
    {"specimen_delivery", "origin", "PREPARE", {"1o"}},
    {"specimen_delivery", "origin", "FINALIZE", {"1c"}},
    {"specimen_delivery", "destination", "PREPARE", {"1o"}},
    {"specimen_delivery", "destination", "FINALIZE", {"1c"}},
    {"logistics_delivery", "origin", "PREPARE", {"1o"}},
    {"logistics_delivery", "origin", "FINALIZE", {"1c"}},
    {"logistics_delivery", "destination", "PREPARE", {"1o"}},
    {"logistics_delivery", "destination", "FINALIZE", {"1c"}},
    {"used_clothes_collection", "origin", "PREPARE", {"5o"}},
    {"used_clothes_collection", "origin", "FINALIZE", {"5c"}},
    {"used_clothes_collection", "destination", "PREPARE", {"5o"}},
    {"used_clothes_collection", "destination", "FINALIZE", {"5c"}},
    {"patient_clothes_return", "destination", "PREPARE", {"o"}},
    {"patient_clothes_return", "destination", "FINALIZE", {"c"}},
    {"battery_low", "destination", "PREPARE", {}},
    {"battery_low", "destination", "FINALIZE", {}},
  };

  for (const auto & item : cases) {
    const auto commands = commandsFor(goalFor(item.task_type, item.stop_type, item.phase));
    EXPECT_EQ(commands, item.commands)
      << item.task_type << "/" << item.stop_type << "/" << item.phase;
  }
}

TEST_F(TaskMotorSequenceNodeTest, PatientRentalFinalizeKeepsDoorCloseAndLiftHome)
{
  auto goal = rentalGoal(1, 1);
  goal.phase = "FINALIZE";

  EXPECT_EQ(commandsFor(goal), (std::vector<std::string>{"c", "hl"}));
}

TEST_F(TaskMotorSequenceNodeTest, RejectsMissingSelectedClothingCommand)
{
  node_->set_parameter(rclcpp::Parameter("top_clothes_step_forward_command", ""));
  auto result = node_->buildAndValidateSequenceForTest(rentalGoal(1, 0));
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message, "motor command parameter is empty: top_clothes_step_forward_command");

  configureMotorCommands();
  node_->set_parameter(rclcpp::Parameter("bottom_clothes_step_forward_command", ""));
  result = node_->buildAndValidateSequenceForTest(rentalGoal(0, 1));
  EXPECT_FALSE(result.success);
  EXPECT_EQ(
    result.message,
    "motor command parameter is empty: bottom_clothes_step_forward_command");
}

TEST_F(TaskMotorSequenceNodeTest, RejectsMissingOrInvalidSequenceLookup)
{
  auto result = node_->buildAndValidateSequenceForTest(
    goalFor("kit_delivery", "origin", "PREPARE"));
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message, "originless task requires stop_type=destination: kit_delivery");

  result = node_->buildAndValidateSequenceForTest(
    goalFor("unknown_delivery", "destination", "PREPARE"));
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message, "unsupported task_type: unknown_delivery");

  result = node_->buildAndValidateSequenceForTest(
    goalFor("logistics_delivery", "destination", "prepare"));
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message, "invalid phase: prepare");
}

}  // namespace
}  // namespace robot_task
