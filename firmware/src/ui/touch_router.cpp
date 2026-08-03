#include "ui/touch_router.h"

#include <cstdlib>

namespace {

enum class State : uint8_t { Idle, PendingTap, Swiping };

constexpr lv_coord_t kTapSlop = 10;
constexpr lv_coord_t kSwipeDetect = 12;
constexpr lv_coord_t kPageChange = 48;
constexpr lv_coord_t kHorizBias = 4;

State s_state = State::Idle;
lv_point_t s_press{};
lv_point_t s_last{};
lv_obj_t* s_active = nullptr;
bool s_click_cancelled = false;
bool s_page_committed = false;
TouchRouterPageStepCb s_page_cb = nullptr;

lv_coord_t absCoord(lv_coord_t v) {
  return v < 0 ? static_cast<lv_coord_t>(-v) : v;
}

void clearActivePressed() {
  if (!s_active) {
    return;
  }
  lv_obj_clear_state(s_active, LV_STATE_PRESSED);
}

void commitPageIfNeeded(lv_coord_t dx, lv_indev_t* indev) {
  if (s_page_committed || !s_page_cb) {
    return;
  }
  const lv_coord_t adx = absCoord(dx);
  if (adx < kPageChange) {
    return;
  }
  s_page_committed = true;
  s_click_cancelled = true;
  clearActivePressed();
  if (dx <= -kPageChange) {
    s_page_cb(1);  // swipe left → next
  } else if (dx >= kPageChange) {
    s_page_cb(-1);  // swipe right → prev
  }
  // After the page actually changes, ignore the rest of this finger-down so the
  // newly visible page / same button cannot receive a CLICKED.
  if (indev) {
    lv_indev_wait_release(indev);
  }
}

void enterSwiping() {
  if (s_state == State::Swiping) {
    return;
  }
  s_state = State::Swiping;
  s_click_cancelled = true;
  clearActivePressed();
}

}  // namespace

void touchRouterBegin() {
  touchRouterReset();
}

void touchRouterSetPageStepHandler(TouchRouterPageStepCb cb) {
  s_page_cb = cb;
}

void touchRouterReset() {
  s_state = State::Idle;
  s_press = {};
  s_last = {};
  s_active = nullptr;
  s_click_cancelled = false;
  s_page_committed = false;
}

bool touchRouterIsSwiping() {
  return s_state == State::Swiping;
}

bool touchRouterSuppressesClick() {
  return s_click_cancelled || s_state == State::Swiping;
}

bool touchRouterAllowsClick() {
  return s_state == State::PendingTap && !s_click_cancelled;
}

bool touchRouterHandleEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t* indev = lv_indev_get_act();

  if (code == LV_EVENT_PRESSED) {
    s_state = State::PendingTap;
    s_click_cancelled = false;
    s_page_committed = false;
    s_active = lv_event_get_target(e);
    if (indev) {
      lv_indev_get_point(indev, &s_press);
      s_last = s_press;
    }
    return false;
  }

  if (code == LV_EVENT_PRESSING) {
    if (!indev) {
      return touchRouterSuppressesClick();
    }
    lv_indev_get_point(indev, &s_last);
    const lv_coord_t dx = static_cast<lv_coord_t>(s_last.x - s_press.x);
    const lv_coord_t dy = static_cast<lv_coord_t>(s_last.y - s_press.y);
    const lv_coord_t adx = absCoord(dx);
    const lv_coord_t ady = absCoord(dy);

    // Any meaningful move kills the click — tighter than swipe detect.
    if (adx > kTapSlop || ady > kTapSlop) {
      s_click_cancelled = true;
      clearActivePressed();
    }

    if (s_state == State::PendingTap && adx >= kSwipeDetect && adx > ady + kHorizBias) {
      enterSwiping();
    }

    if (s_state == State::Swiping) {
      commitPageIfNeeded(dx, indev);
    }
    return touchRouterSuppressesClick();
  }

  if (code == LV_EVENT_RELEASED) {
    const lv_coord_t dx = static_cast<lv_coord_t>(s_last.x - s_press.x);
    if (s_state == State::Swiping || s_page_committed) {
      commitPageIfNeeded(dx, nullptr);
      s_click_cancelled = true;
      s_state = State::Idle;
      return true;
    }
    return s_click_cancelled;
  }

  if (code == LV_EVENT_CLICKED) {
    // Extra guard: any horizontal drag past slop is never a click.
    const lv_coord_t dx = static_cast<lv_coord_t>(s_last.x - s_press.x);
    const lv_coord_t dy = static_cast<lv_coord_t>(s_last.y - s_press.y);
    bool allow = false;
    if (!s_click_cancelled && !s_page_committed &&
        (s_state == State::PendingTap || s_state == State::Idle)) {
      if (absCoord(dx) <= kTapSlop && absCoord(dy) <= kTapSlop) {
        allow = true;
      }
    }
    const bool ignore = !allow;
    touchRouterReset();
    return ignore;
  }

  if (code == LV_EVENT_PRESS_LOST) {
    s_click_cancelled = true;
    touchRouterReset();
    s_click_cancelled = true;
    return true;
  }

  return touchRouterSuppressesClick();
}
