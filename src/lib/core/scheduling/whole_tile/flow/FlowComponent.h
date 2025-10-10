/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

/**
 * @class FlowComponent
 * @brief A collection of tasks which can be scheduled as a single task.
 *
 */
class FlowComponent : public tf::Taskflow
{
public:
  /**
   * @brief Composes this FlowComponent's @ref tf::Taskflow with another @ref tf::Taskflow
   *
   * @param composition @ref tf::Taskflow that is composed of itself
   * and this @ref tf::Taskflow
   */
  void addTo(tf::Taskflow& composition)
  {
    compositionTask_ = composition.composed_of(*this);
  }

  /**
   * @brief Schedule this FlowComponent before another FlowComponent
   *
   * @param successor reference to FlowComponent that will succeed
   * this FlowComponent
   */
  void precede(FlowComponent& successor)
  {
    compositionTask_.precede(successor.compositionTask_);
  }

  /**
   * @brief Schedule this FlowComponent before another FlowComponent
   *
   * @param successor pointer to FlowComponent that will succeed
   * this FlowComponent
   */
  void precede(FlowComponent* successor)
  {
    assert(successor);
    compositionTask_.precede(successor->compositionTask_);
  }

  void precede(tf::Task& successor)
  {
    compositionTask_.precede(successor);
  }

  tf::Task& getCompositionTask(void)
  {
    return compositionTask_;
  }

  void conditional_precede(FlowComponent* root, FlowComponent* successor,
                           std::function<int()> condition_lambda)
  {
    auto condition = root->emplace(condition_lambda).name("condition");

    // Add no-op task
    auto noop = root->emplace([]() {
                      // No operation
                    })
                    .name("noop");

    // Set dependencies: this -> condition -> (successor if 0, no_op if 1)
    precede(condition);
    condition.precede(successor->getCompositionTask(), noop);
  }

  /**
   * @brief Gets name of composition task
   *
   * @param name name of composition task
   * @return FlowComponent* pointing to this
   */
  FlowComponent* name(const std::string& name)
  {
    if(!name.empty())
    {
      compositionTask_.name(name);
    }
    else
    {
      compositionTask_.name("UnnamedFlowComponent");
    }
    return this;
  }

  /**
   * @brief Gets next task placeholder for componentFlow_
   *
   * @return @ref tf::Task&
   */
  tf::Task& nextTask()
  {
    auto task = placeholder();
    componentTasks_.emplace_back(std::move(task));
    return componentTasks_.back();
  }

private:
  /**
   * @brief A std::vector of @ref tf::Task belonging to this FlowComponent
   *
   */
  std::vector<tf::Task> componentTasks_;

  /**
   * @brief @ref tf::Task for the composition of this @ref tf::Taskflow with
   * another @ref tf::Taskflow
   *
   */
  tf::Task compositionTask_;
};
