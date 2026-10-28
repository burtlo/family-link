#ifndef LAB09_PROBE_HPP
#define LAB09_PROBE_HPP

#include "session.hpp"

namespace lab09
{

enum {
    kImageBytes = 28800,
    kPageCap = 16,
    kCardIdCap = 8
};

/* Lab cache and fixture classification. This is not a session writer.
   apply_stage stays on lab07::SessionWriter. */
class Probe
{
  public:
    Probe();

    lab07::SessionWriter &session();
    const lab07::SessionWriter &session() const;

    void sign_in(const char *user_id, int64_t now_ms);
    void sign_out(int64_t now_ms);
    void mute(int64_t now_ms);
    void unmute(int64_t now_ms);
    void arm(const char *scenario);
    lab07::ScopedTicket begin(const char *kind, int64_t now_ms);
    /* Classifies the exchange with the ticket captured at submission, then
       delivers that result. Copies bytes only when this delivery newly applies
       for the user and generation captured on the ticket. */
    int complete(const lab07::ScopedTicket &ticket, int perform_ok, int http_status, const char *body,
                 int body_len, int64_t now_ms);
    void cleanup();

    int image_bytes() const;
    int card_count() const;
    const char *card_at(int index) const;
    int duplicate_cards() const;
    int applied_visible() const;
    const char *active_user() const;
    const char *playback_label() const;
    const char *profile_label() const;

  private:
    void release_image();
    void clear_cards();

    lab07::SessionWriter session_;
    unsigned char image_[kImageBytes];
    int image_bytes_;
    uint64_t image_operation_;
    char cards_[kPageCap][kCardIdCap];
    int card_count_;
    int duplicate_cards_;
    char playback_[16];
    char profile_[16];
};

lab07::ScopedResult classify_scoped_http(const lab07::ScopedTicket &ticket, int perform_ok, int http_status,
                                         const char *body, int body_len);
int collect_card_ids(const char *body, int body_len, char ids[][kCardIdCap], int capacity, int *raw_count);
bool sign_in_response_ok(int perform_ok, int http_status, const char *body, int body_len, const char *user_id);
int emit_scoped_traces(const lab07::SessionWriter &session, char *buffer, int capacity);

} /* namespace lab09 */

#endif
