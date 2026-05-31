#include "write_queue.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

WriteQueue::WriteQueue()
{
  std::memset(queue_, 0, sizeof(queue_));
  head_ = 0;
  tail_ = 0;
  count_ = 0;
}

bool WriteQueue::push(const WriteCommand& cmd)
{
  if (count_ >= MAX_PENDING_WRITES) {
    return false;
  }
  queue_[tail_] = cmd;
  tail_ = (tail_ + 1) % MAX_PENDING_WRITES;
  count_++;
  return true;
}

bool WriteQueue::pop(WriteCommand& cmd_out)
{
  if (count_ == 0) {
    return false;
  }
  cmd_out = queue_[head_];
  head_ = (head_ + 1) % MAX_PENDING_WRITES;
  count_--;
  return true;
}

bool WriteQueue::is_empty() const
{
  return count_ == 0;
}

size_t WriteQueue::size() const
{
  return count_;
}

}  // namespace geappliances_bridge
}  // namespace esphome
