#ifndef LAB07_POLLERS_HPP
#define LAB07_POLLERS_HPP

#include "session.hpp"

namespace lab07
{

/* Roster and health pollers only enqueue observations.
   They do not apply session stage transitions. */

class RosterPoller
{
  public:
    explicit RosterPoller(SessionWriter *session);
    bool submit(const RosterResult &result);

  private:
    SessionWriter *session_;
};

class HealthPoller
{
  public:
    explicit HealthPoller(SessionWriter *session);
    void submit_ack();

  private:
    SessionWriter *session_;
};

} /* namespace lab07 */

#endif
